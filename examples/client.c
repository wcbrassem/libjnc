/**
 * @file client.c
 * @author Roman Janota <xjanot04@fit.vutbr.cz>
 * @brief libnetconf2 client example
 *
 * @copyright
 * Copyright (c) 2022 CESNET, z.s.p.o.
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */

#include "example.h"

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libyang/libyang.h>

#include "log.h"
#include "messages_client.h"
#include "netconf.h"
#include "session_client.h"
#include "session_client_ch.h"

/* Include from libxml2 */
#include <libxml/tree.h>
#include <libxml/xpath.h>

/* Include from libssh */
// #include <libssh/libssh.h>

int nc_client_ssh_set_username(const char *username);
struct nc_session *nc_connect_ssh(const char *host, uint16_t port, struct ly_ctx *ctx);

/**
 * @brief pthread barrier structure.
 */
typedef enum RPC_FORMAT {
    TEXT_STR = 0,   /**< Command string in text, e.g. get-system-information */
    XML_STR,        /**< Command string in text, e.g. <rpc><get-system-information/></rpc> */
    XML_DOC,        /**< Complete RPC connand in XML document tree structure */
    FILE_NAME       /**< File input in either xml RPC or "<command> | display xml RPC" format */
} RPC_FORMAT;

static void
help_print()
{
    printf("Example usage:\n"
            "    ./client -s 10.10.10.10 -u user -p pass -i rpc-request.xml\n"
            "    ./client -s 10.10.10.10 -u user -p pass -o rpc-reply.xml get-chassis-inventory\n"
            "    ./client -s 10.10.10.10 -u user -p pass -x <rpc><get-system-uptime-information/></rpc>\n"
            "\n"
            "    Available options:\n"
            "    -h, --help          Print usage help.\n"
            "    -d, --debug         Enable debugging information.\n"
            "    -x, --xml           RPC command provided in XML format.\n"
            "    -s, --serve         SSH server IP address or domain name.\n"
            "    -t, --tcp           SSH server port number, defaults to 830.\n"
            "    -u, --user          Username for connecting to server.\n"
            "    -p, --pass          Password for connecting to server.\n"
            "    -f, --filter        XPath filter to apply to RPC reply.\n"
            "    -o, --output        Filename to write XML RPC reply.\n"
            "    -i, --input         Filename to read XML RPC request (must be last argument if used).\n\n"
            "    Available RPCs:\n"
            "    get [xpath-filter]\t\t\t\t\t send a <get> RPC with optional XPath filter\n"
            "    get-config [datastore] [xpath-filter]\t\t send a <get-config> RPC with optional XPath filter and datastore, the default datastore is \"running\" \n\n");
}

static enum NC_DATASTORE_TYPE
string2datastore(const char *str)
{
    if (!str) {
        return NC_DATASTORE_RUNNING;
    }

    if (!strcmp(str, "candidate")) {
        return NC_DATASTORE_CANDIDATE;
    } else if (!strcmp(str, "running")) {
        return NC_DATASTORE_RUNNING;
    } else if (!strcmp(str, "startup")) {
        return NC_DATASTORE_STARTUP;
    } else {
        return 0;
    }
}

static int
send_rpc(struct nc_session *session, NC_RPC_TYPE rpc_type, const char *param1, const char *param2)
{
    enum NC_DATASTORE_TYPE datastore;
    int r = 0, rc = 0;
    uint64_t msg_id = 0;
    struct lyd_node *envp = NULL, *op = NULL;
    struct nc_rpc *rpc = NULL;

    /* decide which type of RPC to send */
    switch (rpc_type) {
    case NC_RPC_GET:
        /* create get RPC with an optional filter */
        rpc = nc_rpc_get(param1, NC_WD_UNKNOWN, NC_PARAMTYPE_CONST);
        break;

    case NC_RPC_GETCONFIG:
        /* create get-config RPC with a source datastore and an optional filter */
        datastore = string2datastore(param1);
        if (!datastore) {
            ERR_MSG_CLEANUP("Invalid name of a datastore. Use candidate, running, startup or neither.\n");
        }
        rpc = nc_rpc_getconfig(datastore, param2, NC_WD_UNKNOWN, NC_PARAMTYPE_CONST);
        break;

    default:
        break;
    }
    if (!rpc) {
        ERR_MSG_CLEANUP("Error while creating a RPC\n");
    }

    /* send the RPC on the session and remember NETCONF message ID */

    r = nc_send_rpc(session, rpc, 100, &msg_id);
    if (r != NC_MSG_RPC) {
        ERR_MSG_CLEANUP("Couldn't send a RPC\n");
    }

    /* receive the server's reply with the expected message ID
    * as separate rpc-reply NETCONF envelopes and the parsed YANG output itself, if any */
    r = nc_recv_reply(session, rpc, msg_id, 100, &envp, &op);
    if (r != NC_MSG_REPLY) {
        ERR_MSG_CLEANUP("Couldn't receive a reply from the server\n");
    }

    /* print the whole reply */
    if (!op) {
        r = lyd_print_file(stdout, envp, LYD_XML, 0);
    } else {
        r = lyd_print_file(stdout, op, LYD_XML, 0);
        if (r) {
            ERR_MSG_CLEANUP("Couldn't print the RPC to stdout\n");
        }
        r = lyd_print_file(stdout, envp, LYD_XML, 0);
    }
    if (r) {
        ERR_MSG_CLEANUP("Couldn't print the RPC to stdout\n");
    }

cleanup:
    lyd_free_all(envp);
    lyd_free_all(op);
    nc_rpc_free(rpc);
    return rc;
}

static int
send_rpc_no_schema(struct nc_session *session,  RPC_FORMAT in_format,  const char *in,
                                                RPC_FORMAT out_format, const char *out)
{
    int r = 0, rc = 0;
    uint64_t msg_id = 0;
    struct nc_rpc_no_schema *rpc = NULL;
    xmlDocPtr doc;
    // xmlChar *xmlChar_in = (xmlChar *) in;
    // xmlChar *xmlChar_out = (xmlChar *) out;

    /* decide which type of RPC to send */
    switch(in_format) {
        case TEXT_STR:
            rpc = nc_rpc_no_schema(in, NC_PARAMTYPE_DUP_AND_FREE);
            break;
        case XML_STR:
            rpc = nc_rpc_no_schema_xml(in, NC_PARAMTYPE_DUP_AND_FREE);
            break;
        case FILE_NAME:
            doc = xmlReadFile(in, NULL, 0);
            rpc = nc_rpc_no_schema_doc(doc);
            break;
        default:
            break;
    }

    if (!rpc) {
        ERR_MSG_CLEANUP("Error while creating a RPC\n");
    }

    /* send the RPC on the session and remember NETCONF message ID */

    r = nc_send_rpc(session, (struct nc_rpc *) rpc, 100, &msg_id);
    if (r != NC_MSG_RPC) {
        ERR_MSG_CLEANUP("Couldn't send a RPC\n");
    }

    /* Receive an reply with no schema */
    r = nc_recv_reply_no_schema(session, msg_id, 100, &doc);

    xmlChar *buf;
    int size;
    xmlDocDumpFormatMemory(doc, &buf, &size, 1);
    xmlSaveFormatFile(out, doc, 0);
    xmlFree(buf);

cleanup:
    nc_rpc_free((struct nc_rpc *) rpc);
    return rc;
}

static inline unsigned char *c2uc(char *s) { return (unsigned char*)s; }
static inline char *uc2c(unsigned char *s) { return (char*)s; }

static inline xmlChar *c2xmlc(char *s) { return (unsigned char*)s; }
static inline char *xmlc2c(xmlChar *s) { return (char*)s; }

#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#if defined(LIBXML_XPATH_ENABLED) && defined(LIBXML_SAX1_ENABLED)

/**
 * register_namespaces:
 * @xpathCtx:		the pointer to an XPath context.
 * @nsList:		the list of known namespaces in 
 *			"<prefix1>=<href1> <prefix2>=href2> ..." format.
 *
 * Registers namespaces from @nsList in @xpathCtx.
 *
 * Returns 0 on success and a negative value otherwise.
 */
int 
register_namespaces(xmlXPathContextPtr xpathCtx, const xmlChar* nsList) {
    xmlChar* nsListDup;
    xmlChar* prefix;
    xmlChar* href;
    xmlChar* next;
    
    // assert(xpathCtx);
    // assert(nsList);

    nsListDup = xmlStrdup(nsList);
    if(nsListDup == NULL) {
	fprintf(stderr, "Error: unable to strdup namespaces list\n");
	return(-1);	
    }
    
    next = nsListDup; 
    while(next != NULL) {
	/* skip spaces */
	while((*next) == ' ') next++;
	if((*next) == '\0') break;

	/* find prefix */
	prefix = next;
	next = (xmlChar*)xmlStrchr(next, '=');
	if(next == NULL) {
	    fprintf(stderr,"Error: invalid namespaces list format\n");
	    xmlFree(nsListDup);
	    return(-1);	
	}
	*(next++) = '\0';	
	
	/* find href */
	href = next;
	next = (xmlChar*)xmlStrchr(next, ' ');
	if(next != NULL) {
	    *(next++) = '\0';	
	}

	/* do register namespace */
	if(xmlXPathRegisterNs(xpathCtx, prefix, href) != 0) {
	    fprintf(stderr,"Error: unable to register NS with prefix=\"%s\" and href=\"%s\"\n", prefix, href);
	    xmlFree(nsListDup);
	    return(-1);	
	}
    }
    
    xmlFree(nsListDup);
    return(0);
}

/**
 * print_xpath_nodes:
 * @nodes:		the nodes set.
 * @output:		the output file handle.
 *
 * Prints the @nodes content to @output.
 */
void
print_xpath_nodes(xmlNodeSetPtr nodes, FILE* output) {
    xmlNodePtr cur;
    int size;
    int i;
    
    // assert(output);
    size = (nodes) ? nodes->nodeNr : 0;
    
    fprintf(output, "Result (%d nodes):\n", size);
    for(i = 0; i < size; ++i) {
	// assert(nodes->nodeTab[i]);
	
	if(nodes->nodeTab[i]->type == XML_NAMESPACE_DECL) {
	    xmlNsPtr ns;
	    
	    ns = (xmlNsPtr)nodes->nodeTab[i];
	    cur = (xmlNodePtr)ns->next;
	    if(cur->ns) { 
	        fprintf(output, "= namespace \"%s\"=\"%s\" for node %s:%s\n", 
		    ns->prefix, ns->href, cur->ns->href, cur->name);
	    } else {
	        fprintf(output, "= namespace \"%s\"=\"%s\" for node %s\n", 
		    ns->prefix, ns->href, cur->name);
	    }
	} else if(nodes->nodeTab[i]->type == XML_ELEMENT_NODE) {
	    cur = nodes->nodeTab[i];   	    
	    if(cur->ns) { 
    	        fprintf(output, "= element node \"%s:%s\"\n", 
		    cur->ns->href, cur->name);
	    } else {
    	        fprintf(output, "= element node \"%s\"\n", 
		    cur->name);
	    }
	} else {
	    cur = nodes->nodeTab[i];    
	    fprintf(output, "= node \"%s\": type %d\n", cur->name, cur->type);
	}
    }
}

int testxml(const char *filename,const xmlChar* xpathExpr, const xmlChar* nsList)
{
    // char *filename = "/Users/wbrassem/Documents/Code/libjnc/examples/system-users.xml";
    xmlDocPtr doc;
    xmlXPathContextPtr xpathCtx; 
    xmlXPathObjectPtr xpathObj; 

    /* Load XML document */
    doc = xmlParseFile(filename);
    if (doc == NULL) {
        fprintf(stderr, "Error: unable to parse file \"%s\"\n", filename);
        return(-1);
    }

    /* Create xpath evaluation context */
    xpathCtx = xmlXPathNewContext(doc);
    if(xpathCtx == NULL) {
        fprintf(stderr,"Error: unable to create new XPath context\n");
        xmlFreeDoc(doc); 
        return(-1);
    }
    
    /* Register namespaces from list (if any) */
    if((nsList != NULL) && (register_namespaces(xpathCtx, nsList) < 0)) {
        fprintf(stderr,"Error: failed to register namespaces list \"%s\"\n", nsList);
        xmlXPathFreeContext(xpathCtx); 
        xmlFreeDoc(doc); 
        return(-1);
    }

    /* Evaluate xpath expression */
    xpathObj = xmlXPathEvalExpression(xpathExpr, xpathCtx);
    if(xpathObj == NULL) {
        fprintf(stderr,"Error: unable to evaluate xpath expression \"%s\"\n", xpathExpr);
        xmlXPathFreeContext(xpathCtx); 
        xmlFreeDoc(doc); 
        return(-1);
    }

    /* Print results */
    print_xpath_nodes(xpathObj->nodesetval, stdout);

    /* Cleanup */
    xmlXPathFreeObject(xpathObj);
    xmlXPathFreeContext(xpathCtx); 
    xmlFreeDoc(doc); 

    return 0;
}

#endif

int
main(int argc, char **argv)
{
    // testxml("/Users/wbrassem/Documents/Code/libjnc/examples/system-users.xml",
    //             c2xmlc("//nc:rpc-reply"),
    //             c2xmlc("nc=urn:ietf:params:xml:ns:netconf:base:1.0"));
 
    // testxml("/Users/wbrassem/Documents/Code/libjnc/examples/system-users.xml",
    //             c2xmlc("//active-user-count[@junos:format]"),
    //             // c2xmlc("nc=urn:ietf:params:xml:ns:netconf:base:1.0"));
    //             c2xmlc("junos=http://xml.juniper.net/junos/22.2R0/junos"));

    // testxml("/Users/wbrassem/Documents/Code/libjnc/examples/get-l2ckt-connection-information.xml",
    //             c2xmlc("//jns:l2circuit-neighbor"),
    //             c2xmlc("nc=urn:ietf:params:xml:ns:netconf:base:1.0 jns=http://xml.juniper.net/junos/22.2R0/junos-routing"));

    testxml("/Users/wbrassem/Documents/Code/libjnc/examples/running-configuration.xml",
                c2xmlc("//*[local-name()='groups']"),
                // c2xmlc("//*[local-name()='label-switched-path']"),
                c2xmlc(NULL));

    int rc = 0, opt;
    struct nc_session *session = NULL;
    const char *ssh_server = NULL, *ssh_server_port_str = NULL;
    const char *xpath_filter = NULL;
    const char *in = NULL, *out = NULL;
    const char *rpc_parameter_1 = NULL, *rpc_parameter_2 = NULL;
    long ssh_server_port = SSH_PORT;
    RPC_FORMAT in_format = TEXT_STR, out_format = TEXT_STR;

    struct option options[] = {
        {"help",    no_argument,        NULL, 'h'},
        {"debug",   no_argument,        NULL, 'd'},
        {"xml",     no_argument,        NULL, 'x'},
        {"server",  required_argument,  NULL, 's'},
        {"tcp",     required_argument,  NULL, 't'},
        {"user",    required_argument,  NULL, 'u'},
        {"pass",    required_argument,  NULL, 'p'},
        {"filter",  required_argument,  NULL, 'f'},
        {"out",     required_argument,  NULL, 'o'},
        {"in",      no_argument,        NULL, 'i'},
        {NULL,      0,                  NULL,  0}
    };

    // Three more protential switches
    // 1. Change purpose of -x to XML input - done
    // 2. Add -y for strict IETF YANG model processing
    // 3. Add -j for JUNOS no schema (the default setting)
    // 4. Add -i to pass in a XML file containing an RPC - done
    // 5. Add -o to pass a filename to write the reply to - done
    // 6. Add -f to provide and XPath filter
    // Not an argv but somehow create an XML document and then send it to server - done

    if (argc == 1) {
        help_print();
        goto cleanup;
    }

    nc_client_init();
    /* set the path to search for schemas */
    nc_client_set_schema_searchpath(MODULES_DIR);

    opterr = 0;

    while ((opt = getopt_long(argc, argv, "hdxs:t:u:p:f:o:i", options, NULL)) != -1) {
        switch (opt) {
        case 'h':
            help_print();
            goto cleanup;

        case 'd':
            nc_verbosity(NC_VERB_DEBUG);
            break;

        case 'x':
            in_format = XML_STR;
            break;

        case 's':
            ssh_server = optarg;
            break;

        case 't':
            ssh_server_port_str = optarg;
            if (!(ssh_server_port = strtol(ssh_server_port_str,NULL,10)))
                ssh_server_port = SSH_PORT;
            break;

        case 'u':
            /* set the client SSH username to be used when connecting to the server */
            if (nc_client_ssh_set_username(optarg)) {
                ERR_MSG_CLEANUP("Couldn't set the SSH username\n");
            }
            break;

        case 'p':
            /* set the client SSH password to be used when connecting to the server */
            if (nc_client_ssh_set_password(optarg)) {
                ERR_MSG_CLEANUP("Couldn't set the SSH password\n");
            }
            break;

        case 'f':
            xpath_filter = optarg;
            break;

        case 'i':
            in_format = FILE_NAME;
            break;

        case 'o':
            out = optarg;
            out_format = FILE_NAME;
            break;

        default:
            ERR_MSG_CLEANUP("Invalid option or missing argument\n");
        }
    }

    if (optind == argc) {
        ERR_MSG_CLEANUP("Expected the name of RPC after options\n");
    }

    /* Open the ssh session to the server */
    session = nc_connect_ssh(ssh_server, ssh_server_port, NULL);
    
    if (!session) {
        ERR_MSG_CLEANUP("Couldn't connect to the server\n");
    }

    /* sending a get RPC */

    // if (!strcmp(argv[optind], "get")) {
    //     if (optind + 1 < argc) {
    //         /* use the specified XPath filter */
    //         rpc_parameter_1 = argv[optind + 1];
    //     }
    //     if (send_rpc(session, NC_RPC_GET, rpc_parameter_1, rpc_parameter_2)) {
    //         rc = 1;
    //         goto cleanup;
    //     }
    //     /* sending a get-config RPC */
    // } else if (!strcmp(argv[optind], "get-config")) {
    //     /* use the specified datastore and optional XPath filter */
    //     if (optind + 2 < argc) {
    //         rpc_parameter_1 = argv[optind + 1];
    //         rpc_parameter_2 = argv[optind + 2];
    //     } else if (optind + 1 < argc) {
    //         rpc_parameter_1 = argv[optind + 1];
    //     }
    //     if (send_rpc(session, NC_RPC_GETCONFIG, rpc_parameter_1, rpc_parameter_2)) {
    //         rc = 1;
    //         goto cleanup;
    //     }

    // } else {
        in = argv[optind];

        if (send_rpc_no_schema(session, in_format, in, out_format, out)) {
            rc = 1;
            goto cleanup;
        }
    // }

cleanup:
    nc_client_close(session);
    return rc;
}