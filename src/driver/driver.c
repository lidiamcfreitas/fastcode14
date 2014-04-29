#include <fusion.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>
//#include <stdint-gcc.h> Not available on RedHat6@CAB. Not needed.
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <getopt.h>
#include <math.h>

#include "result_file.h"
#include "testconfig.h"
#include "image_io.h"

#define TIFF_DEBUG_IN "gradient.tif"
#define TIFF_DEBUG_OUT "out.tif"
#define TIFF_DEBUG_OUT2 "gradient.o.tif"

typedef struct {
    char* log_file;
    char* out_file;
    char* val_file;
} cli_options_t;

/*
 * GENERAL I/O
 **/
const char* usage_str = "./driver [options] <heights> <widths>"
        "<contrastParm> <saturationParm> <wellexpParm>\n\n"
        "options:\n"
        " --testlibtiff:\n"
        "  performs some tests using libtiff"
        " --log <file>\n"
        "  write measurement results into log file <file>.\n"
        "  (otherwise only the error is calculated and the result is abandoned)"
        "\n"
        " --validate <reference>\n"
        "  compare calculated result against <reference>.\n"
        " --store <outputFile>\n"
        "  store result into <outputFile>\n"
        "\n";

void print_usage() {
    printf("%s", usage_str);
    exit(1);
}

/*
 * PERFORMANCE MEASUREMENT
 **/
void run_testconfiguration( result_t* result, testconfig_t* tc ) {
    uint32_t w, h;
    size_t img_count;
    double** input_images = tc_read_input_images( &img_count, &w, &h, tc );
#ifdef DEBUG
    printf("img_count: %ld, w: %d, h: %d\n", img_count, w, h);
#endif
    //
    // size_t npixels = w * h;
    // ret_image = (double*) malloc( h*w*sizeof(double) );
    // alloc_fusion( h, w, img_count, &segments );
    // alloc_fusion( ... )
    // exposure_fusion( input_images, r, c, N, {tc[i].contrast,
    // tc[i].saturation, tc[i].exposure}, ret_image,

    // convert result into tiff raster
    // uint32_t* raster = rgb2tiff( ret_image, npixels );


    // compare to reference
    // result->error = compare_tif(raster, w, h, tc->ref_path );

    // runtime and deviatation from reference solution

    // free resources
    // free_tiff( raster );
    // free( ret_image );
    tc_free_input_images( input_images, img_count );
    //free_fusion( &segments );
}

int parse_cli(cli_options_t* cli_opts, testconfig_t* testconfig,
                          int argc, char* argv[]) {

    // getopt for command-line parsing. See the getopt(3) manpage
    int c;
    while (true) {
        static struct option long_options[] = {
            {"testlibtiff", no_argument,       0, 't'},
            {"log",         required_argument, 0, 'l'},
            {"store",       required_argument, 0, 's'},
            {"validate",    required_argument, 0, 'v'},
            {0,0,0,0}
        };

        int option_index = 0;
        c = getopt_long(argc, argv, "t", long_options, &option_index);
        if (c == -1) { // -1 indicates end of options reached
            break;
        }
        switch (c) {
        case 0:
            // the long option with name long_options[option_index].name is
            // found
            printf("getopt error on long option %s\n",
                   long_options[option_index].name);
            break;

        case 't':
            printf("getopt: testlibtiff\n");
            debug_tiff_test( TIFF_DEBUG_IN, TIFF_DEBUG_OUT );

            uint32_t dbg_w, dbg_h;
            double *debug_rgb_image = load_tiff_rgb( &dbg_w, &dbg_h,
                                                     TIFF_DEBUG_IN );
            printf( "dbg_w: %d, dbg_h: %d\n", dbg_w, dbg_h );
            store_tiff_rgb( debug_rgb_image, dbg_w, dbg_h, TIFF_DEBUG_OUT2 );

            uint32_t* raster = rgb2tiff(debug_rgb_image, dbg_w*dbg_h);
            double err = compare_tif(raster, dbg_w, dbg_h, TIFF_DEBUG_IN );
            printf("error is: %lf\n", err);
            free_tiff( raster );
            free_rgb( debug_rgb_image );
            break;
        case 's':
            cli_opts->out_file = optarg;
            break;
        case 'v':
            cli_opts->val_file = optarg;
            break;
        case 'l': // log
            cli_opts->log_file = optarg;
            break;

        case '?':
            printf("getopt: error on character %c\n", optopt);
            break;

        default:
            printf("getopt: general error\n");
            abort();
        }
    }

    int num_args_remaining = argc - optind;
    int ret_optind, tmp_optind = optind;
    if( (ret_optind = read_testconfiguration(testconfig, num_args_remaining,
                                             &argv[optind])) < 0 ) {
        int err_arg = tmp_optind + abs(ret_optind) - 1;
        fprintf(stderr, "error while parsing argument %d: %s\n", err_arg+1,
               argv[err_arg]);
        return -err_arg;
    } else if ( ret_optind < 5 ) {
        fprintf( stderr, "too few arguments\n" );
        return -argc;
    }
    return ret_optind + tmp_optind;
    // next time, we use a language that supports exceptions for the driver code
    // ;-)
}

/*
 * MAIN
 **/

/**
 * @brief main
 * @param argc
 * @param argv
 * @return
 */
int main(int argc, char* argv[]) {
    testconfig_t testconfig =
    { {0,0,0},
      {0,0,0},
      0.0,
      0.0,
      0.0,
      0,
      NULL,
      NULL
    };

    cli_options_t cli_opts = {
        NULL,
        NULL,
        NULL
    };

    //result_t result;
    if ( parse_cli( &cli_opts, &testconfig, argc, argv ) > 0 ) {
#ifndef NDEBUG
        print_testconfiguration( &testconfig );
        if( cli_opts.log_file )
            printf("log file: %s\n", cli_opts.log_file );
        if( cli_opts.log_file )
            printf("log file: %s\n", cli_opts.log_file );
        if( cli_opts.log_file )
            printf("log file: %s\n", cli_opts.log_file );
#endif
    //run_testconfiguration( &result, &testconfig );
    }
}
