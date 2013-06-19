#include <limits.h>
#include <stdint.h>
#include "cmd.h"

#define PRINTSET_DECL(x) int print_##x(struct cli_state *, int, char **); int set_##x(struct cli_state *, int, char **);
#define PRINTSET_ENTRY(x) { .print = print_##x, .set = set_##x, .name = #x }

/* An entry in the printset table */
struct printset_entry {
    int (*print)(struct cli_state *s, int argc, char **argv);  /*<< Function pointer to thing that prints parameter */
    int (*set)(struct cli_state *s, int argc, char **argv);    /*<< Function pointer to setter */
    const char *name;                   /*<< String associated with parameter */
};

/* Declarations */
PRINTSET_DECL(bandwidth)
PRINTSET_DECL(config)
PRINTSET_DECL(frequency)
PRINTSET_DECL(gpio)
PRINTSET_DECL(lmsregs)
PRINTSET_DECL(loopback)
PRINTSET_DECL(mimo)
PRINTSET_DECL(pa)
PRINTSET_DECL(pps)
PRINTSET_DECL(refclk)
PRINTSET_DECL(rxvga1)
PRINTSET_DECL(rxvga2)
PRINTSET_DECL(samplerate)
PRINTSET_DECL(trimdac)
PRINTSET_DECL(txvga1)
PRINTSET_DECL(txvga2)


/* print/set parameter table */
struct printset_entry printset_table[] = {
    PRINTSET_ENTRY(bandwidth),
    PRINTSET_ENTRY(config),
    PRINTSET_ENTRY(frequency),
    PRINTSET_ENTRY(gpio),
    PRINTSET_ENTRY(lmsregs),
    PRINTSET_ENTRY(loopback),
    PRINTSET_ENTRY(mimo),
    PRINTSET_ENTRY(pa),
    PRINTSET_ENTRY(pps),
    PRINTSET_ENTRY(refclk),
    PRINTSET_ENTRY(rxvga1),
    PRINTSET_ENTRY(rxvga2),
    PRINTSET_ENTRY(samplerate),
    PRINTSET_ENTRY(trimdac),
    PRINTSET_ENTRY(txvga1),
    PRINTSET_ENTRY(txvga2),
    { .print = NULL, .set = NULL, .name = "" }
};

int print_bandwidth(struct cli_state *state, int argc, char **argv)
{
    /* Usage: print bandwidth [<rx|tx>]*/
    int rv = CMD_RET_OK, ret;
    bladerf_module module = RX ;
    unsigned int bw ;
    if( argc == 3 ) {
        if( strcasecmp( argv[2], "rx" ) == 0 ) {
            module = RX;
        } else if( strcasecmp( argv[2], "tx" ) == 0 ) {
            module = TX;
        } else {
            printf( "%s: %s is not TX or RX - printing both\n", argv[0], argv[2] );
        }
    }

    printf( "\n" ) ;

    if( argc == 2 || module == RX ) {
        /* TODO: Check ret */
        ret = bladerf_get_bandwidth( state->curr_device, RX, &bw );
        printf( "  RX Bandwidth: %9uHz\n", bw );
    }

    if( argc == 2 || module == TX ) {
        /* TODO: Check ret */
        ret = bladerf_get_bandwidth( state->curr_device, TX, &bw );
        printf( "  TX Bandwidth: %9uHz\n", bw );
    }

    printf( "\n" );

    return rv;
}

int set_bandwidth(struct cli_state *state, int argc, char **argv)
{
    /* Usage: set bandwidth [rx|tx] <bandwidth in Hz> */
    int rv = CMD_RET_OK, ret;
    bladerf_module module = RX;
    unsigned int bw = 28000000, actual;

    /* Check for extended help */
    if( argc == 2 ) {
        printf( "\n" );
        printf( "Usage: set bandwidth [module] <bandwidth>\n" );
        printf( "\n" );
        printf( "    module         Optional argument to set single module bandwidth\n" );
        printf( "    bandwidth      Bandwidth in Hz - will be rounded up to closest bandwidth\n" );
        printf( "\n" );
    }

    /* Check for optional module */
    else if( argc == 4 ) {
        bool ok;
        if( strcasecmp( argv[2], "rx" ) == 0 ) {
            module = RX;
        } else if( strcasecmp( argv[2], "tx" ) == 0 ) {
            module = TX;
        } else {
            rv = CMD_RET_INVPARAM;
        }
        /* Parse bandwidth */
        bw = str2uint( argv[3], 0, UINT_MAX, &ok );
        if( !ok ) {
            printf( "%s: %s is not a valid bandwidth\n", argv[0], argv[3] );
            rv = CMD_RET_INVPARAM;
        }
    }

    /* No module, just bandwidth */
    else if( argc == 3 ) {
        bool ok;
        bw = str2uint( argv[2], 0, UINT_MAX, &ok );
        if( !ok ) {
            printf( "%s: %s is not a valid bandwidth\n", argv[0], argv[2] );
            rv = CMD_RET_INVPARAM;
        }
    }

    /* Problem parsing arguments? */
    if( rv != CMD_RET_OK ) goto finish ;

    printf( "\n" );

    /* Lack of option, so set both or RX only */
    if( argc == 3 || module == RX ) {
        /* TODO: Check ret */
        ret = bladerf_set_bandwidth( state->curr_device, RX, bw, &actual );
        printf( "  Setting RX bandwidth - req:%9uHz actual:%9uHz\n",  bw, actual );
    }

    /* Lack of option, so set both or TX only */
    if( argc == 3 || module == TX ) {
        /* TODO: Check ret */
        ret = bladerf_set_bandwidth( state->curr_device, TX, bw, &actual );
        printf( "  Setting TX bandwidth - req:%9uHz actual:%9uHz\n", bw, actual );
    }

    printf( "\n" );

finish:
    return rv;
}

int print_config(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_config(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_frequency(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_frequency(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_gpio(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_gpio(struct cli_state *state, int argc, char **argv)
{
    /* set gpio <value> */
    int rv = CMD_RET_OK;
    uint32_t val;
    bool ok;
    if( argc == 3 ) {
        val = str2uint( argv[2], 0, UINT_MAX, &ok );
        if( !ok ) {
            printf( "%s %s: %s is not a valid value\n", argv[0], argv[1], argv[2] );
            rv = CMD_RET_INVPARAM;
        } else {
            gpio_write( state->curr_device,val );
        }
    } else {
        printf( "set gpio specialized help\n" );
    }
    return rv;
}


int print_lmsregs(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_lmsregs(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_loopback(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_loopback(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_mimo(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_mimo(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_pa(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_pa(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_pps(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_pps(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_refclk(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_refclk(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_rxvga1(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_rxvga1(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_rxvga2(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_rxvga2(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_samplerate(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_samplerate(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_trimdac(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_trimdac(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_txvga1(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_txvga1(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int print_txvga2(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

int set_txvga2(struct cli_state *state, int argc, char **argv)
{
    return CMD_RET_OK;
}

struct printset_entry *get_printset_entry( char *name)
{
    int i;
    struct printset_entry *entry = NULL;
    for( i = 0; entry == NULL && printset_table[i].print != NULL; ++i ) {
        if( strcasecmp( name, printset_table[i].name ) == 0 ) {
            entry = &(printset_table[i]);
        }
    }
    return entry;
}

/* Set command */
int cmd_set(struct cli_state *state, int argc, char **argv)
{
    /* Valid commands:
        set bandwidth <rx|tx> <bw in Hz>
        set frequency <rx|tx> <frequency in Hz>
        set gpio <value>
        set loopback <mode> [options]
        set mimo <master|slave|off>
        set pa <lb|hb>
        set pps <on|off>
        set refclk <reference frequency> <comparison frequency>
        set rxvga1 <gain in dB>
        set rxvga2 <gain in dB>
        set samplerate <integer Hz> [<num frac> <denom frac>]
        set trimdac <value>
        set txvga1 <gain in dB>
        set txvga2 <gain in dB>

       NOTE: Using set <parameter> with no other entries should print
       a nice usage note for that specific setting.
    */
    int rv = CMD_RET_OK;
    if( argc > 1 ) {
        struct printset_entry *entry = NULL;

        entry = get_printset_entry( argv[1] );

        if( entry ) {
            /* Call the parameter setting function */
            rv = entry->set(state, argc, argv);
        } else {
            /* Incorrect parameter to print */
            printf( "%s: %s is not a valid parameter to print\n", argv[0], argv[1] );
            rv = CMD_RET_INVPARAM;
        }
    } else {
        printf( "%s: Invalid number of parameters (%d)\n", argv[0], argc);
        rv = CMD_RET_INVPARAM;
    }
    return rv;
}

/* Print command */
int cmd_print(struct cli_state *state, int argc, char **argv)
{
    /* Valid commands:
        print bandwidth <rx|tx>
        print config
        print frequency
        print gpio
        print lmsregs
        print lna
        print loopback
        print mimo
        print pa
        print pps
        print refclk
        print rxvga1
        print rxvga2
        print samplerate
        print trimdac
        print txvga1
        print txvga2
    */
    int rv = CMD_RET_OK;
    struct printset_entry *entry = NULL;

    entry = get_printset_entry( argv[1] );

    if( entry ) {
        /* Call the parameter printing function */
        rv = entry->print(state, argc, argv);
    } else {
        /* Incorrect parameter to print */
        printf( "%s: %s is not a valid parameter to print\n", argv[0], argv[1] );
        rv = CMD_RET_INVPARAM;
    }
    return rv;
}

