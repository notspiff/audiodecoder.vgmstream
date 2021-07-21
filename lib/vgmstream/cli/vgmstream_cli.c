#define POSIXLY_CORRECT
#include <getopt.h>
#include "../src/vgmstream.h"
#include "../src/plugins.h"
#include "../src/util.h"
#ifdef WIN32
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif

#ifndef VERSION
#include "version.h"
#endif
#ifndef VERSION
#define VERSION "(unknown version)"
#endif

#ifdef HAVE_JSON
#include "jansson/jansson.h"
#endif

/* low values are ok as there is very little performance difference, but higher
 * may improve write I/O in some systems as this*channels doubles as output buffer */
#define SAMPLE_BUFFER_SIZE  32768

/* getopt globals (the horror...) */
extern char* optarg;
extern int optind, opterr, optopt;


static size_t make_wav_header(uint8_t* buf, size_t buf_size, int32_t sample_count, int32_t sample_rate, int channels, int smpl_chunk, int32_t loop_start, int32_t loop_end);

static void usage(const char* name, int is_full) {
    fprintf(stderr,"vgmstream CLI decoder " VERSION " " __DATE__ "\n"
            "Usage: %s [-o <outfile.wav>] [options] <infile>\n"
            "Options:\n"
            "    -o <outfile.wav>: name of output .wav file, default <infile>.wav\n"
            "       <outfile> wildcards can be ?s=subsong, ?n=stream name, ?f=infile\n"
            "    -l loop count: loop count, default 2.0\n"
            "    -f fade time: fade time in seconds after N loops, default 10.0\n"
            "    -d fade delay: fade delay in seconds, default 0.0\n"
            "    -F: don't fade after N loops and play the rest of the stream\n"
            "    -i: ignore looping information and play the whole stream once\n"
            "    -e: force end-to-end looping\n"
            "    -E: force end-to-end looping even if file has real loop points\n"
            "    -s N: select subsong N, if the format supports multiple subsongs\n"
            "    -m: print metadata only, don't decode\n"
            "    -L: append a smpl chunk and create a looping wav\n"
            "    -2 N: only output the Nth (first is 0) set of stereo channels\n"
            "    -p: output to stdout (for piping into another program)\n"
            "    -P: output to stdout even if stdout is a terminal\n"
            "    -c: loop forever (continuously) to stdout\n"
            "    -x: decode and print adxencd command line to encode as ADX\n"
            "    -g: decode and print oggenc command line to encode as OGG\n"
            "    -b: decode and print batch variable commands\n"
            "    -h: print extra commands (for testing)\n"
#ifdef HAVE_JSON
            "    -V: print version info and supported extensions as JSON\n"
            "    -I: print requested file info as JSON\n"
#endif
            , name);
    if (!is_full)
        return;
    fprintf(stderr,
            "    -v: validate extensions (for extension testing)\n"
            "    -r: output a second file after resetting (for reset testing)\n"
            "    -k N: seeks to N samples before decoding (for seek testing)\n"
            "    -K N: seeks again to N samples before decoding (for seek testing)\n"
            "    -t file: print tags found in file (for tag testing)\n"
            "    -T: print title (for title testing)\n"
            "    -D <max channels>: downmix to <max channels> (for plugin downmix testing)\n"
            "    -O: decode but don't write to file (for performance testing)\n"
    );

}


typedef struct {
    char* infilename;
    char* outfilename;
    char* tag_filename;
    int play_forever;
    int play_sdtout;
    int play_wreckless;
    int print_metaonly;
#ifdef HAVE_JSON
    int print_metajson;
#endif
    int print_adxencd;
    int print_oggenc;
    int print_batchvar;
    int write_lwav;
    int only_stereo;
    int stream_index;

    double loop_count;
    double fade_time;
    double fade_delay;
    int ignore_fade;
    int ignore_loop;
    int force_loop;
    int really_force_loop;

    int validate_extensions;
    int test_reset;
    int seek_samples1;
    int seek_samples2;
    int decode_only;
    int show_title;
    int downmix_channels;

    /* not quite config but eh */
    int lwav_loop_start;
    int lwav_loop_end;
} cli_config;
#ifdef HAVE_JSON
static void print_json_version();
static void print_json_info(VGMSTREAM* vgm, cli_config* cfg);
#endif


static int parse_config(cli_config* cfg, int argc, char** argv) {
    int opt;

    /* non-zero defaults */
    cfg->only_stereo = -1;
    cfg->loop_count = 2.0;
    cfg->fade_time = 10.0;
    cfg->seek_samples1 = -1;
    cfg->seek_samples2 = -1;

    /* don't let getopt print errors to stdout automatically */
    opterr = 0;

    /* read config */
    while ((opt = getopt(argc, argv, "o:l:f:d:ipPcmxeLEFrgb2:s:t:Tk:K:hOvD:"
#ifdef HAVE_JSON
        "VI"
#endif
    )) != -1) {
        switch (opt) {
            case 'o':
                cfg->outfilename = optarg;
                break;
            case 'l':
                cfg->loop_count = atof(optarg);
                break;
            case 'f':
                cfg->fade_time = atof(optarg);
                break;
            case 'd':
                cfg->fade_delay = atof(optarg);
                break;
            case 'i':
                cfg->ignore_loop = 1;
                break;
            case 'p':
                cfg->play_sdtout = 1;
                break;
            case 'P':
                cfg->play_wreckless = 1;
                cfg->play_sdtout = 1;
                break;
            case 'c':
                cfg->play_forever = 1;
                break;
            case 'm':
                cfg->print_metaonly = 1;
                break;
            case 'x':
                cfg->print_adxencd = 1;
                break;
            case 'g':
                cfg->print_oggenc = 1;
                break;
            case 'b':
                cfg->print_batchvar = 1;
                break;
            case 'e':
                cfg->force_loop = 1;
                break;
            case 'E':
                cfg->really_force_loop = 1;
                break;
            case 'L':
                cfg->write_lwav = 1;
                break;
            case 'r':
                cfg->test_reset = 1;
                break;
            case '2':
                cfg->only_stereo = atoi(optarg);
                break;
            case 'F':
                cfg->ignore_fade = 1;
                break;
            case 's':
                cfg->stream_index = atoi(optarg);
                break;
            case 't':
                cfg->tag_filename= optarg;
                break;
            case 'T':
                cfg->show_title = 1;
                break;
            case 'k':
                cfg->seek_samples1 = atoi(optarg);
                break;
            case 'K':
                cfg->seek_samples2 = atoi(optarg);
                break;
            case 'O':
                cfg->decode_only = 1;
                break;
            case 'v':
                cfg->validate_extensions = 1;
                break;
            case 'D':
                cfg->downmix_channels = atoi(optarg);
                break;
            case 'h':
                usage(argv[0], 1);
                goto fail;
#ifdef HAVE_JSON
            case 'V':
                print_json_version();
                goto fail;
            case 'I':
                cfg->print_metaonly = 1;
                cfg->print_metajson = 1;
                break;
#endif
            case '?':
                fprintf(stderr, "Unknown option -%c found\n", optopt);
                goto fail;
            default:
                usage(argv[0], 0);
                goto fail;
        }
    }

    /* filename goes last */
    if (optind != argc - 1) {
        usage(argv[0], 0);
        goto fail;
    }
    cfg->infilename = argv[optind];


    return 1;
fail:
    return 0;
}

static int validate_config(cli_config* cfg) {
    if (cfg->play_sdtout && (!cfg->play_wreckless && isatty(STDOUT_FILENO))) {
        fprintf(stderr,"Are you sure you want to output wave data to the terminal?\nIf so use -P instead of -p.\n");
        goto fail;
    }
    if (cfg->play_forever && !cfg->play_sdtout) {
        fprintf(stderr,"-c must use -p or -P\n");
        goto fail;
    }
    if (cfg->play_sdtout && cfg->outfilename) {
        fprintf(stderr,"use either -p or -o\n");
        goto fail;
    }

    /* other options have built-in priority defined */

    return 1;
fail:
    return 0;
}

static void print_info(VGMSTREAM* vgmstream, cli_config* cfg) {
    int channels = vgmstream->channels;
    if (!cfg->play_sdtout) {
        if (cfg->print_adxencd) {
            printf("adxencd");
            if (!cfg->print_metaonly)
                printf(" \"%s\"",cfg->outfilename);
            if (vgmstream->loop_flag)
                printf(" -lps%d -lpe%d",vgmstream->loop_start_sample,vgmstream->loop_end_sample);
            printf("\n");
        }
        else if (cfg->print_oggenc) {
            printf("oggenc");
            if (!cfg->print_metaonly)
                printf(" \"%s\"",cfg->outfilename);
            if (vgmstream->loop_flag)
                printf(" -c LOOPSTART=%d -c LOOPLENGTH=%d",vgmstream->loop_start_sample, vgmstream->loop_end_sample-vgmstream->loop_start_sample);
            printf("\n");
        }
        else if (cfg->print_batchvar) {
            if (!cfg->print_metaonly)
                printf("set fname=\"%s\"\n",cfg->outfilename);
            printf("set tsamp=%d\nset chan=%d\n", vgmstream->num_samples, channels);
            if (vgmstream->loop_flag)
                printf("set lstart=%d\nset lend=%d\nset loop=1\n", vgmstream->loop_start_sample, vgmstream->loop_end_sample);
            else
                printf("set loop=0\n");
        }
        else if (cfg->print_metaonly) {
            printf("metadata for %s\n",cfg->infilename);
        }
        else {
            printf("decoding %s\n",cfg->infilename);
        }
    }

    if (!cfg->play_sdtout && !cfg->print_adxencd && !cfg->print_oggenc && !cfg->print_batchvar) {
        char description[1024];
        description[0] = '\0';
        describe_vgmstream(vgmstream,description,1024);
        printf("%s",description);
    }
}


static void apply_config(VGMSTREAM* vgmstream, cli_config* cfg) {
    vgmstream_cfg_t vcfg = {0};

    /* write loops in the wav, but don't actually loop it */
    if (cfg->write_lwav) {
        vcfg.disable_config_override = 1;
        cfg->ignore_loop = 1;

        if (vgmstream->loop_start_sample < vgmstream->loop_end_sample) {
            cfg->lwav_loop_start = vgmstream->loop_start_sample;
            cfg->lwav_loop_end = vgmstream->loop_end_sample;
            cfg->lwav_loop_end--; /* from spec, +1 is added when reading "smpl" */
        }

    }
    /* only allowed if manually active */
    if (cfg->play_forever) {
        vcfg.allow_play_forever = 1;
    }

    vcfg.play_forever = cfg->play_forever;
    vcfg.fade_time = cfg->fade_time;
    vcfg.loop_count = cfg->loop_count;
    vcfg.fade_delay = cfg->fade_delay;

    vcfg.ignore_loop  = cfg->ignore_loop;
    vcfg.force_loop = cfg->force_loop;
    vcfg.really_force_loop = cfg->really_force_loop;
    vcfg.ignore_fade = cfg->ignore_fade;

    vgmstream_apply_config(vgmstream, &vcfg);
}


static void print_tags(cli_config* cfg) {
    VGMSTREAM_TAGS* tags = NULL;
    STREAMFILE* sf_tags = NULL;
    const char *tag_key, *tag_val;

    if (!cfg->tag_filename)
        return;

    sf_tags = open_stdio_streamfile(cfg->tag_filename);
    if (!sf_tags) {
        printf("tag file %s not found\n", cfg->tag_filename);
        return;
    }

    printf("tags:\n");

    tags = vgmstream_tags_init(&tag_key, &tag_val);
    vgmstream_tags_reset(tags, cfg->infilename);
    while (vgmstream_tags_next_tag(tags, sf_tags)) {
        printf("- '%s'='%s'\n", tag_key, tag_val);
    }

    vgmstream_tags_close(tags);
    close_streamfile(sf_tags);
}

static void print_title(VGMSTREAM* vgmstream, cli_config* cfg) {
    char title[1024];
    vgmstream_title_t tcfg = {0};

    if (!cfg->show_title)
        return;

    tcfg.force_title = 0;
    tcfg.subsong_range = 0;
    tcfg.remove_extension = 0;

    vgmstream_get_title(title, sizeof(title), cfg->infilename, vgmstream, &tcfg);

    printf("title: %s\n", title);
}

#ifdef HAVE_JSON
void print_json_version() {
    size_t extension_list_len;
    size_t common_extension_list_len;
    const char** extension_list;
    const char** common_extension_list;
    extension_list = vgmstream_get_formats(&extension_list_len);
    common_extension_list = vgmstream_get_common_formats(&common_extension_list_len);

    json_t* ext_list = json_array();
    json_t* cext_list = json_array();

    for (size_t i = 0; i < extension_list_len; ++i) {
        json_t* ext = json_string(extension_list[i]);
        json_array_append(ext_list, ext);
    }

    for (size_t i = 0; i < common_extension_list_len; ++i) {
        json_t* cext = json_string(common_extension_list[i]);
        json_array_append(cext_list, cext);
    }

    json_t* version_string = json_string(VERSION);

    json_t* final_object = json_object();
    json_object_set(final_object, "version", version_string);
    json_decref(version_string);

    json_object_set(final_object, "extensions",
                    json_pack("{soso}",
                              "vgm", ext_list,
                              "common", cext_list));

    json_dumpf(final_object, stdout, JSON_COMPACT);
}
#endif

static void clean_filename(char* dst, int clean_paths) {
    int i;
    for (i = 0; i < strlen(dst); i++) {
        char c = dst[i];
        int is_badchar = (clean_paths && (c == '\\' || c == '/'))
            || c == '*' || c == '?' || c == ':' /*|| c == '|'*/ || c == '<' || c == '>';
        if (is_badchar)
            dst[i] = '_';
    }
}

/* replaces a filename with "?n" (stream name), "?f" (infilename) or "?s" (subsong) wildcards
 * ("?" was chosen since it's not a valid Windows filename char and hopefully nobody uses it on Linux) */
void replace_filename(char* dst, size_t dstsize, const char* outfilename, const char* infilename, VGMSTREAM* vgmstream) {
    int subsong;
    char stream_name[PATH_LIMIT];
    char buf[PATH_LIMIT];
    char tmp[PATH_LIMIT];
    int i;


    /* file has a "%" > temp replace for sprintf */
    strcpy(buf, outfilename);
    for (i = 0; i < strlen(buf); i++) {
        if (buf[i] == '%')
            buf[i] = '|'; /* non-valid filename, not used in format */
    }

    /* init config */
    subsong = vgmstream->stream_index;
    if (subsong > vgmstream->num_streams) {
        subsong = 0; /* for games without subsongs */
    }

    if (vgmstream->stream_name && vgmstream->stream_name[0] != '\0') {
        snprintf(stream_name, sizeof(stream_name), "%s", vgmstream->stream_name);
        clean_filename(stream_name, 1); /* clean subsong name's subdirs */
    }
    else {
        snprintf(stream_name, sizeof(stream_name), "%s", infilename);
        clean_filename(stream_name, 0); /* don't clean user's subdirs */
    }

    /* do controlled replaces of each wildcard (in theory could appear N times) */
    do {
        char* pos = strchr(buf, '?');
        if (!pos)
            break;

        /* use buf as format and copy formatted result to tmp (assuming sprintf's format must not overlap with dst) */
        if (pos[1] == 'n') {
            pos[0] = '%';
            pos[1] = 's'; /* use %s */
            snprintf(tmp, sizeof(tmp), buf, stream_name);
        }
        else if (pos[1] == 'f') {
            pos[0] = '%';
            pos[1] = 's'; /* use %s */
            snprintf(tmp, sizeof(tmp), buf, infilename);
        }
        else if (pos[1] == 's') {
            pos[0] = '%';
            pos[1] = 'i'; /* use %i */
            snprintf(tmp, sizeof(tmp), buf, subsong);
        }
        else if ((pos[1] == '0' && pos[2] >= '1' && pos[2] <= '9' && pos[3] == 's')) {
            pos[0] = '%';
            pos[3] = 'i'; /* use %0Ni */
            snprintf(tmp, sizeof(tmp), buf, subsong);
        }
        else {
            /* not recognized */
            continue;
        }

        /* copy result to buf again, so it can be used as format in next replace
         * (can be optimized with some pointer swapping but who cares about a few extra nanoseconds) */
        strcpy(buf, tmp);
    }
    while (1);

    /* replace % back */
    for (i = 0; i < strlen(buf); i++) {
        if (buf[i] == '|')
            buf[i] = '%';
    }

    snprintf(dst, dstsize, "%s", buf);
}


/* ************************************************************ */

int main(int argc, char** argv) {
    VGMSTREAM* vgmstream = NULL;
    FILE* outfile = NULL;
    char outfilename_temp[PATH_LIMIT];

    sample_t* buf = NULL;
    int channels, input_channels;
    int32_t len_samples;
    int i, j;

    cli_config cfg = {0};
    int res;


    /* read args */
    res = parse_config(&cfg, argc, argv);
    if (!res) goto fail;

#ifdef WIN32
    /* make stdout output work with windows */
    if (cfg.play_sdtout) {
        _setmode(fileno(stdout),_O_BINARY);
    }
#endif

    res = validate_config(&cfg);
    if (!res) goto fail;


    /* for plugin testing */
    if (cfg.validate_extensions)  {
        int valid;
        vgmstream_ctx_valid_cfg vcfg = {0};

        vcfg.skip_standard = 0;
        vcfg.reject_extensionless = 0;
        vcfg.accept_unknown = 0;
        vcfg.accept_common = 0;

        valid = vgmstream_ctx_is_valid(cfg.infilename, &vcfg);
        if (!valid) goto fail;
    }

    /* open streamfile and pass subsong */
    {
        STREAMFILE* sf = open_stdio_streamfile(cfg.infilename);
        if (!sf) {
            fprintf(stderr,"file %s not found\n",cfg.infilename);
            goto fail;
        }

        sf->stream_index = cfg.stream_index;
        vgmstream = init_vgmstream_from_STREAMFILE(sf);
        close_streamfile(sf);

        if (!vgmstream) {
            fprintf(stderr,"failed opening %s\n",cfg.infilename);
            goto fail;
        }
    }


    /* modify the VGMSTREAM if needed (before printing file info) */
    apply_config(vgmstream, &cfg);

    channels = vgmstream->channels;
    input_channels = vgmstream->channels;

    /* enable after config but before outbuf */
    if (cfg.downmix_channels)
        vgmstream_mixing_autodownmix(vgmstream, cfg.downmix_channels);
    vgmstream_mixing_enable(vgmstream, SAMPLE_BUFFER_SIZE, &input_channels, &channels);

    /* get final play config */
    len_samples = vgmstream_get_samples(vgmstream);
    if (len_samples <= 0)
        goto fail;

    if (cfg.play_forever && !vgmstream_get_play_forever(vgmstream)) {
        fprintf(stderr,"File can't be played forever");
        goto fail;
    }


    /* prepare output */
    if (cfg.play_sdtout) {
        outfile = stdout;
    }
    else if (!cfg.print_metaonly  && !cfg.decode_only) {
        if (!cfg.outfilename) {
            /* note that outfilename_temp must persist outside this block, hence the external array */
            strcpy(outfilename_temp, cfg.infilename);
            strcat(outfilename_temp, ".wav");
            cfg.outfilename = outfilename_temp;
            /* maybe should avoid overwriting with this auto-name, for the unlikely
             * case of file header-body pairs (file.ext+file.ext.wav) */
        }
        else if (strchr(cfg.outfilename, '?') != NULL) {
            /* special substitution */
            replace_filename(outfilename_temp, sizeof(outfilename_temp), cfg.outfilename, cfg.infilename, vgmstream);
            cfg.outfilename = outfilename_temp;
        }

        /* don't overwrite itself! */
        if (strcmp(cfg.outfilename, cfg.infilename) == 0) {
            fprintf(stderr,"same infile and outfile name: %s\n", cfg.outfilename);
            goto fail;
        }

        outfile = fopen(cfg.outfilename,"wb");
        if (!outfile) {
            fprintf(stderr,"failed to open %s for output\n", cfg.outfilename);
            goto fail;
        }

        /* no improvement */
        //setvbuf(outfile, NULL, _IOFBF, SAMPLE_BUFFER_SIZE * sizeof(sample_t) * input_channels);
        //setvbuf(outfile, NULL, _IONBF, 0);
    }


    /* prints */
#ifdef HAVE_JSON
    if (!cfg.print_metajson) {
#endif
        print_info(vgmstream, &cfg);
        print_tags(&cfg);
        print_title(vgmstream, &cfg);
#ifdef HAVE_JSON
    }
    else {
        print_json_info(vgmstream, &cfg);
    }
#endif

    /* prints done */
    if (cfg.print_metaonly) {
        if (!cfg.play_sdtout) {
            if (outfile != NULL)
                fclose(outfile);
        }
        close_vgmstream(vgmstream);
        return EXIT_SUCCESS;
    }

    if (cfg.seek_samples1 < -1) /* ex value for loop testing */
        cfg.seek_samples1 = vgmstream->loop_start_sample;
    if (cfg.seek_samples1 >= len_samples)
        cfg.seek_samples1 = -1;
    if (cfg.seek_samples2 >= len_samples)
        cfg.seek_samples2 = -1;

    if (cfg.seek_samples2 >= 0)
        len_samples -= cfg.seek_samples2;
    else if (cfg.seek_samples1 >= 0)
        len_samples -= cfg.seek_samples1;


    /* last init */
    buf = malloc(SAMPLE_BUFFER_SIZE * sizeof(sample_t) * input_channels);
    if (!buf) {
        fprintf(stderr,"failed allocating output buffer\n");
        goto fail;
    }

    /* decode forever */
    while (cfg.play_forever) {
        int to_get = SAMPLE_BUFFER_SIZE;

        render_vgmstream(buf, to_get, vgmstream);

        swap_samples_le(buf, channels * to_get); /* write PC endian */
        if (cfg.only_stereo != -1) {
            for (j = 0; j < to_get; j++) {
                fwrite(buf + j*channels + (cfg.only_stereo*2), sizeof(sample_t), 2, outfile);
            }
        } else {
            fwrite(buf, sizeof(sample_t) * channels, to_get, outfile);
        }
    }


    /* slap on a .wav header */
    if (!cfg.decode_only) {
        uint8_t wav_buf[0x100];
        int channels_write = (cfg.only_stereo != -1) ? 2 : channels;
        size_t bytes_done;

        bytes_done = make_wav_header(wav_buf,0x100,
                len_samples, vgmstream->sample_rate, channels_write,
                cfg.write_lwav, cfg.lwav_loop_start, cfg.lwav_loop_end);

        fwrite(wav_buf,sizeof(uint8_t),bytes_done,outfile);
    }


    if (cfg.seek_samples1 >= 0)
        seek_vgmstream(vgmstream, cfg.seek_samples1);
    if (cfg.seek_samples2 >= 0)
        seek_vgmstream(vgmstream, cfg.seek_samples2);

    /* decode */
    for (i = 0; i < len_samples; i += SAMPLE_BUFFER_SIZE) {
        int to_get = SAMPLE_BUFFER_SIZE;
        if (i + SAMPLE_BUFFER_SIZE > len_samples)
            to_get = len_samples - i;

        render_vgmstream(buf, to_get, vgmstream);

        if (!cfg.decode_only) {
            swap_samples_le(buf, channels * to_get); /* write PC endian */
            if (cfg.only_stereo != -1) {
                for (j = 0; j < to_get; j++) {
                    fwrite(buf + j*channels + (cfg.only_stereo*2), sizeof(sample_t), 2, outfile);
                }
            } else {
                fwrite(buf, sizeof(sample_t), to_get * channels, outfile);
            }
        }
    }

    if (outfile != NULL) {
        fclose(outfile);
        outfile = NULL;
    }


    /* try again with (for testing reset_vgmstream, simulates a seek to 0 after changing internal state) */
    if (cfg.test_reset) {
        char outfilename_reset[PATH_LIMIT];
        strcpy(outfilename_reset, cfg.outfilename);
        strcat(outfilename_reset, ".reset.wav");

        outfile = fopen(outfilename_reset,"wb");
        if (!outfile) {
            fprintf(stderr,"failed to open %s for output\n",outfilename_reset);
            goto fail;
        }

        /* slap on a .wav header */
        if (!cfg.decode_only) {
            uint8_t wav_buf[0x100];
            int channels_write = (cfg.only_stereo != -1) ? 2 : channels;
            size_t bytes_done;

            bytes_done = make_wav_header(wav_buf,0x100,
                    len_samples, vgmstream->sample_rate, channels_write,
                    cfg.write_lwav, cfg.lwav_loop_start, cfg.lwav_loop_end);

            fwrite(wav_buf,sizeof(uint8_t),bytes_done,outfile);
        }


        reset_vgmstream(vgmstream);

        if (cfg.seek_samples1 >= 0)
            seek_vgmstream(vgmstream, cfg.seek_samples1);
        if (cfg.seek_samples2 >= 0)
            seek_vgmstream(vgmstream, cfg.seek_samples2);

        /* decode */
        for (i = 0; i < len_samples; i += SAMPLE_BUFFER_SIZE) {
            int to_get = SAMPLE_BUFFER_SIZE;
            if (i + SAMPLE_BUFFER_SIZE > len_samples)
                to_get = len_samples - i;

            render_vgmstream(buf, to_get, vgmstream);

            if (!cfg.decode_only) {
                swap_samples_le(buf, channels * to_get); /* write PC endian */
                if (cfg.only_stereo != -1) {
                    for (j = 0; j < to_get; j++) {
                        fwrite(buf + j*channels + (cfg.only_stereo*2), sizeof(sample_t), 2, outfile);
                    }
                } else {
                    fwrite(buf, sizeof(sample_t) * channels, to_get, outfile);
                }
            }
        }

        if (outfile != NULL) {
            fclose(outfile);
            outfile = NULL;
        }
    }

    close_vgmstream(vgmstream);
    free(buf);

    return EXIT_SUCCESS;

fail:
    if (!cfg.play_sdtout) {
        if (outfile != NULL)
            fclose(outfile);
    }
    close_vgmstream(vgmstream);
    free(buf);
    return EXIT_FAILURE;
}

#ifdef HAVE_JSON
static void print_json_info(VGMSTREAM* vgm, cli_config* cfg) {
    json_t* version_string = json_string(VERSION);
    vgmstream_info info;
    describe_vgmstream_info(vgm, &info);

    json_t* mixing_info = NULL;

    // The JSON pack format string is defined here: https://jansson.readthedocs.io/en/latest/apiref.html#building-values

    if (info.mixing_info.input_channels > 0) {
        mixing_info = json_pack("{sisi}",
            "inputChannels", info.mixing_info.input_channels,
            "outputChannels", info.mixing_info.output_channels);
    }

    json_t* loop_info = NULL;

    if (info.loop_info.end > info.loop_info.start) {
        loop_info = json_pack("{sisi}",
            "start", info.loop_info.start,
            "end", info.loop_info.end);
    }

    json_t* interleave_info = NULL;

    if (info.interleave_info.last_block > info.interleave_info.first_block) {
        interleave_info = json_pack("{sisi}",
            "firstBlock", info.interleave_info.first_block,
            "lastBlock", info.interleave_info.last_block
        );
    }
    
    json_t* stream_info = json_pack("{sisssi}",
        "index", info.stream_info.current,
        "name", info.stream_info.name,
        "total", info.stream_info.total
    );

    if (info.stream_info.name[0] == '\0') {
        json_object_set(stream_info, "name", json_null());
    }

    json_t* final_object = json_pack(
        "{sssisiso?siso?so?sisssssisssiso?}",
        "version", version_string,
        "sampleRate", info.sample_rate,
        "channels", info.channels,
        "mixingInfo", mixing_info,
        "channelLayout", info.channel_layout,
        "loopingInfo", loop_info,
        "interleaveInfo", interleave_info,
        "numberOfSamples", info.num_samples,
        "encoding", info.encoding,
        "layout", info.layout,
        "frameSize", info.frame_size,
        "metadataSource", info.metadata,
        "bitrate", info.bitrate,
        "streamInfo", stream_info
    );

    if (info.frame_size == 0) {
        json_object_set(final_object, "frameSize", json_null());
    }

    if (info.channel_layout == 0) {
        json_object_set(final_object, "channelLayout", json_null());
    }
    
    json_dumpf(final_object, stdout, JSON_COMPACT);

    json_decref(final_object);
}
#endif

static void make_smpl_chunk(uint8_t* buf, int32_t loop_start, int32_t loop_end) {
    int i;

    memcpy(buf+0, "smpl", 0x04); /* header */
    put_s32le(buf+0x04, 0x3c); /* size */

    for (i = 0; i < 7; i++)
        put_s32le(buf+0x08 + i * 0x04, 0);

    put_s32le(buf+0x24, 1);

    for (i = 0; i < 3; i++)
        put_s32le(buf+0x28 + i * 0x04, 0);

    put_s32le(buf+0x34, loop_start);
    put_s32le(buf+0x38, loop_end);
    put_s32le(buf+0x3C, 0);
    put_s32le(buf+0x40, 0);
}

/* make a RIFF header for .wav */
static size_t make_wav_header(uint8_t* buf, size_t buf_size, int32_t sample_count, int32_t sample_rate, int channels, int smpl_chunk, int32_t loop_start, int32_t loop_end) {
    size_t data_size, header_size;

    data_size = sample_count * channels * sizeof(sample_t);
    header_size = 0x2c;
    if (smpl_chunk && loop_end)
        header_size += 0x3c+ 0x08;

    if (header_size > buf_size)
        goto fail;

    memcpy(buf+0x00, "RIFF", 0x04); /* RIFF header */
    put_32bitLE(buf+0x04, (int32_t)(header_size - 0x08 + data_size)); /* size of RIFF */

    memcpy(buf+0x08, "WAVE", 4); /* WAVE header */

    memcpy(buf+0x0c, "fmt ", 0x04); /* WAVE fmt chunk */
    put_s32le(buf+0x10, 0x10); /* size of WAVE fmt chunk */
    put_s16le(buf+0x14, 0x0001); /* codec PCM */
    put_s16le(buf+0x16, channels); /* channel count */
    put_s32le(buf+0x18, sample_rate); /* sample rate */
    put_s32le(buf+0x1c, sample_rate * channels * sizeof(sample_t)); /* bytes per second */
    put_s16le(buf+0x20, (int16_t)(channels * sizeof(sample_t))); /* block align */
    put_s16le(buf+0x22, sizeof(sample_t) * 8); /* significant bits per sample */

    if (smpl_chunk && loop_end) {
        make_smpl_chunk(buf+0x24, loop_start, loop_end);
        memcpy(buf+0x24+0x3c+0x08, "data", 0x04); /* WAVE data chunk */
        put_u32le(buf+0x28+0x3c+0x08, (int32_t)data_size); /* size of WAVE data chunk */
    }
    else {
        memcpy(buf+0x24, "data", 0x04); /* WAVE data chunk */
        put_s32le(buf+0x28, (int32_t)data_size); /* size of WAVE data chunk */
    }

    /* could try to add channel_layout, but would need to write WAVEFORMATEXTENSIBLE (maybe only if arg flag?) */

    return header_size;
fail:
    return 0;
}
