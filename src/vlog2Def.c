//----------------------------------------------------------------
// vlog2Def
//----------------------------------------------------------------
// Convert from verilog netlist to a pre-placement DEF file
//----------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>	/* for getopt() */
#include <math.h>	/* For sqrt() and ceil() */

#include "hash.h"
#include "readverilog.h"
#include "readlef.h"

void write_output(struct cellrec *, int hasmacros, float aspect, int units,
		GATE coresite);
void helpmessage(FILE *outf);

/* Linked list for nets */

typedef struct _linkedNet *linkedNetPtr;

typedef struct _linkedNet {
    char *instname;
    char *pinname;
    linkedNetPtr next;
} linkedNet;

/* Hash table of LEF macros */
struct hashtable LEFhash;

/*--------------------------------------------------------------*/

int main (int argc, char *argv[])
{
    int i, result, hasmacros = FALSE;
    int units = 100;
    float aspect = 1.0;
    struct cellrec *topcell;
    GATE coresite = NULL;

    InitializeHashTable(&LEFhash, SMALLHASHSIZE);

    while ((i = getopt(argc, argv, "hHl:a:u:")) != EOF) {
        switch (i) {
	    case 'h':
	    case 'H':
		helpmessage(stdout);
		return 0;
	    case 'l':
		result = LefRead(optarg);	/* Can be called multiple times */
		if (result == 0) {
		    helpmessage(stderr);
		    return 1;
		}
		break;
	    case 'a':
		if (sscanf(optarg, "%f", aspect) != 1) {
		    fprintf(stderr, "Could not read aspect value from \"-a %s\"\n",
				optarg);
		    helpmessage(stderr);
		    return 1;
		}
		break;
	    case 'u':
		if (sscanf(optarg, "%d", units) != 1) {
		    fprintf(stderr, "Could not read units value from \"-u %s\"\n",
				optarg);
		    helpmessage(stderr);
		    return 1;
		}
	    default:
		fprintf(stderr, "Bad option switch \"%c\"\n", (char)i);
		helpmessage(stderr);
		return 1;
   	}
    }

    if (optind >= argc) {
	fprintf(stderr, "Couldn't find a filename as input\n");
	helpmessage(stderr);
	return 1;
    }

    /* If any LEF files were read, hash the GateInfo list */
    if (GateInfo != NULL) {
	GATE gate;
	for (gate = GateInfo; gate; gate = gate->next) {
	    HashPtrInstall(gate->gatename, gate, &LEFhash);
	    if (!strncmp(gate->gatename, "site_", 5))
		if (gate->gateclass == MACRO_CLASS_CORE)
		    coresite = gate;
	}
	hasmacros = TRUE;
    }

    topcell = ReadVerilog(argv[optind]);
    write_output(topcell, hasmacros, aspect, units, coresite);
    return 0;
}

/*--------------------------------------------------------------*/
/* output_nets:							*/
/* Recursion callback function for each item in Nodehash	*/
/*--------------------------------------------------------------*/

struct nlist *output_nets(struct hashlist *p, void *cptr)
{
    struct netrec *net;
    FILE *outf = (FILE *)cptr;
    linkedNetPtr nlink, nsrch;

    nlink = (linkedNetPtr)(p->ptr);

    fprintf(outf, "- %s\n", p->name);

    for (nsrch = nlink; nsrch; nsrch = nsrch->next) {
	fprintf(outf, "  ( %s %s )", nsrch->instname, nsrch->pinname);
	if (nsrch->next == NULL)
	    fprintf(outf, " ;");
	fprintf(outf, "\n");
    }

    return NULL;
}

/*--------------------------------------------------------------*/
/* write_output							*/
/*								*/
/*         ARGS: 						*/
/*      RETURNS: 1 to OS					*/
/* SIDE EFFECTS: 						*/
/*--------------------------------------------------------------*/

void write_output(struct cellrec *topcell, int hasmacros, float aspect, int units,
	GATE coresite)
{
    FILE *outfptr = stdout;
    int ncomp, npin, nnet, start, end, i;
    int totalwidth, totalheight, rowwidth, rowheight, numrows;
    int sitewidth, siteheight, numsites;
    char portnet[512];

    struct netrec *net;
    struct portrec *port;
    struct instance *inst;

    struct hashtable Nodehash;

    /* Static string "PIN" for ports */
    static char pinname[] = "PIN";

    /* This string array much match the port definitions in readverilog.h */
    static char *portdirs[] = {"", "INPUT", "OUTPUT", "INOUT"};

    linkedNetPtr nlink, nsrch;

    /* Hash the nets */

    InitializeHashTable(&Nodehash, LARGEHASHSIZE);
    nnet = 0;
    for (port = topcell->portlist; port; port = port->next) {
	if ((net = BusHashLookup(port->name, &topcell->nets)) != NULL) {
	    start = net->start;
	    end = net->end;
	}
	else start = end = -1;

	if (start > end) {
	    int tmp;
	    tmp = start;
	    start = end;
	    end = tmp;
	}
	for (i = start; i <= end; i++) {
	    nlink = (linkedNetPtr)malloc(sizeof(linkedNet));
	    nlink->instname = pinname;

	    if (start == -1)
		nlink->pinname = port->name;
	    else {
		sprintf(portnet, "%s[%d]", port->name, i);
		nlink->pinname = strdup(portnet);
	    }
	    nlink->next = NULL;
	    if ((nsrch = HashLookup(portnet, &Nodehash)) != NULL) {
		while (nsrch->next) nsrch = nsrch->next;
		nsrch->next = nlink;
	    }
	    else {
		HashPtrInstall(portnet, nlink, &Nodehash);
		nnet++;
	    }
	}
    }
    for (inst = topcell->instlist; inst; inst = inst->next) {
	for (port = inst->portlist; port; port = port->next) {

	    nlink = (linkedNetPtr)malloc(sizeof(linkedNet));
	    nlink->instname = inst->instname;
	    nlink->pinname = port->name;
	    nlink->next = NULL;

	    if ((nsrch = HashLookup(port->net, &Nodehash)) != NULL) {
		while (nsrch->next) nsrch = nsrch->next;
		nsrch->next = nlink;
	    }
	    else {
		HashPtrInstall(port->net, nlink, &Nodehash);
		nnet++;
	    }

	    if (hasmacros) {
		GATE gate;
	
		gate = HashLookup(inst->cellname, &LEFhash);
		if (gate) {
		    /* Make sure this is a core cell */
		    if (gate->gateclass == MACRO_CLASS_CORE) {
			totalwidth += (int)(gate->width * (float)units);
			rowheight = (int)(gate->height * (float)units);
		    }
		    /* To do:  Handle non-core cell records */
		}
	    }
	}
    }

    /* Write output DEF header */
    fprintf(outfptr, "VERSION 5.6 ;\n");
    fprintf(outfptr, "NAMECASESENSITIVE ON  ;\n");
    fprintf(outfptr, "DIVIDERCHAR \"/\" ;\n");
    fprintf(outfptr, "BUSBITCHARS \"[]\" ;\n");
    fprintf(outfptr, "DESIGN %s ;\n", topcell->name);
    fprintf(outfptr, "UNITS DISTANCE MICRONS %d ;\n", units);
    fprintf(outfptr, "\n");

    /* Calculate pre-placement die area, rows, and tracks, and output the same,	*/
    /* depending on what has been read in from LEF files.			*/

    if (hasmacros) {
	numrows = (int)ceilf(sqrtf(totalwidth / (aspect * rowheight)));
	rowwidth = (int)ceilf(totalwidth / numrows);
	totalheight = (int)ceilf(rowheight * numrows);
	sitewidth = (int)(coresite->width * units);
	siteheight = (int)(coresite->height * units);

	/* To do: compute additional area for pins */

	fprintf(outfptr, "DIEAREA ( 0 0 ) ( %d %d ) ;\n", rowwidth, totalheight);
	fprintf(outfptr, "\n");

        /* Compute site placement and generate ROW statements */

	numsites = (int)ceilf((float)rowwidth / (float)sitewidth);
	for (i = 0; i < numrows; i++) {
	    fprintf(outfptr, "ROW ROW_%d %s 0 %d %c DO %d BY 1 STEP %d 0 ;\n",
			i + 1, coresite->gatename + 5, i * siteheight,
			((i % 2) == 0) ? 'N' : 'S', numsites, sitewidth);
	}
	fprintf(outfptr, "\n");
    }

    /* Write components in the order of the input file */

    ncomp = 0;
    for (inst = topcell->instlist; inst; inst = inst->next) ncomp++;
    fprintf(outfptr, "COMPONENTS %d ;\n", ncomp);

    for (inst = topcell->instlist; inst; inst = inst->next)
	fprintf(outfptr, "- %s %s ;\n", inst->instname, inst->cellname);
    fprintf(outfptr, "END COMPONENTS\n\n");

    npin = 0;
    for (port = topcell->portlist; port; port = port->next) {
	if ((net = BusHashLookup(port->name, &topcell->nets)) != NULL) {
	    int btot = net->start - net->end;
	    if (btot < 0) btot = -btot;
	    npin = btot + 1;
	}
	else
	    npin++;
    }
    fprintf(outfptr, "PINS %d ;\n", npin);

    for (port = topcell->portlist; port; port = port->next) {
	if ((net = BusHashLookup(port->name, &topcell->nets)) != NULL) {
	    if (net->start == -1) {
		fprintf(outfptr, "- %s + NET %s", port->name, port->name);
		if (port->direction == PORT_NONE)
		    fprintf(outfptr, " ;\n");
		else
		    fprintf(outfptr, "\n  + DIRECTION %s ;\n", portdirs[port->direction]);
	    }
	    else if (net->start > net->end) {
		for (i = net->start; i >= net->end; i--) {
		    fprintf(outfptr, "- %s[%d] + NET %s[%d]", port->name, i,
				port->name, i);
		    if (port->direction == PORT_NONE)
			fprintf(outfptr, " ;\n");
		    else
			fprintf(outfptr, "\n  + DIRECTION %s ;\n",
					portdirs[port->direction]);
		}
	    }
	    else {
		for (i = net->start; i <= net->end; i++) {
		    fprintf(outfptr, "- %s[%d] + NET %s[%d]", port->name, i,
				port->name, i);
		    if (port->direction == PORT_NONE)
			fprintf(outfptr, " ;\n");
		    else
			fprintf(outfptr, "\n  + DIRECTION %s ;\n",
					portdirs[port->direction]);
		}
	    }
	}
	else {
	    fprintf(outfptr, "- %s + NET %s", port->name, port->name);
	    if (port->direction == PORT_NONE)
		fprintf(outfptr, " ;\n");
	    else
		fprintf(outfptr, "\n  + DIRECTION %s ;\n", portdirs[port->direction]);
	}
    }

    fprintf(outfptr, "END PINS\n\n");

    fprintf(outfptr, "NETS %d ;\n", nnet);
    RecurseHashTablePointer(&Nodehash, output_nets, outfptr);
    fprintf(outfptr, "END NETS\n\n");

    /* End the design */
    fprintf(outfptr, "END DESIGN\n");
    fflush(stdout);

} /* write_output */

/*--------------------------------------------------------------*/
/* C helpmessage - tell user how to use the program		*/
/*								*/
/*         ARGS: 						*/
/*      RETURNS: 1 to OS					*/
/* SIDE EFFECTS: 						*/
/*--------------------------------------------------------------*/

void helpmessage(FILE *outf)
{
    fprintf(outf,"vlog2Def <netlist>\n");
    fprintf(outf,"\n");
    fprintf(outf,"vlog2Def converts a verilog netlist to a pre-placement DEF file.\n");
    fprintf(outf,"\n");
    fprintf(outf,"options:\n");
    fprintf(outf,"\n");
    fprintf(outf,"   -h          Print this message\n");
    fprintf(outf,"   -l <path>   Read LEF file from <path> (may be called multiple"
			" times)\n");
    fprintf(outf,"   -a <value>	 Set aspect ratio to <value> (default 1.0)\n");
    fprintf(outf,"   -u <value>  Set units-per-micron to <value) (default 100)\n");

} /* helpmessage() */

