/* ----------------------------- MNI Header -----------------------------------
@NAME       : dcm2mnc.c
@DESCRIPTION: Program to convert dicom files to minc
@GLOBALS    : 
@CREATED    : June 2001 (Rick Hoge)
@MODIFIED   : 
 * $Log: dcm2mnc.c,v $
 * Revision 1.2  2005-03-02 18:23:32  bert
 * Added mosaic sequence and bitwise options
 *
 * Revision 1.1  2005/02/17 16:38:09  bert
 * Initial checkin, revised DICOM to MINC converter
 *
 * Revision 1.1.1.1  2003/08/15 19:52:55  leili
 * Leili's dicom server for sonata
 *
 * Revision 1.5  2002/04/26 12:02:50  rhoge
 * updated usage statement for new forking defaults
 *
 * Revision 1.4  2002/04/26 11:32:48  rhoge
 * made forking default
 *
 * Revision 1.3  2002/03/23 13:17:53  rhoge
 * added support for Bourget network pushed dicom files, cleaned up
 * file check and read_numa4_dicom vr check/assignment
 *
 * Revision 1.2  2002/03/22 19:19:36  rhoge
 * Numerous fixes -
 * - handle Numaris 4 Dicom patient name
 * - option to cleanup input files
 * - command option
 * - list-only option
 * - debug mode
 * - user supplied name, idstr
 * - anonymization
 *
 * Revision 1.1  2002/03/22 03:50:02  rhoge
 * new name for standalone dicom to minc converter
 *
 * Revision 1.3  2002/03/22 00:38:08  rhoge
 * Added progress bar, wait for children at end, updated feedback statements
 *
 * Revision 1.2  2002/03/19 13:13:56  rhoge
 * initial working mosaic support - I think time is scrambled though.
 *
 * Revision 1.1  2001/12/31 17:26:21  rhoge
 * adding file to repository- compiles without warning and converts non-mosaic
 * Numa 4 files. 
 * Will probably not work for Numa 3 files yet.
 *
---------------------------------------------------------------------------- */

#ifndef lint
static char rcsid[]="$Header: /private-cvsroot/minc/conversion/dcm2mnc/dcm2mnc.c,v 1.2 2005-03-02 18:23:32 bert Exp $";
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <ParseArgv.h>

#define GLOBAL_ELEMENT_DEFINITION /* To define elements */
#include "dcm2mnc.h"

/* Function Prototypes */
static int ima_sort_function(const void *entry1, const void *entry2);
static int dcm_sort_function(const void *entry1, const void *entry2);
static void use_the_files(int num_files, 
                          const char *file_list[], 
                          Data_Object_Info *data_info[],
                          const char *out_dir);
static void usage(void);
static void free_list(int num_files, 
                      const char **file_list, 
                      Data_Object_Info **file_info_list);
static int check_file_type_consistency(int num_files, const char *file_list[]);


struct globals G;

ArgvInfo argTable[] = {
    {"-clobber", ARGV_CONSTANT, (char *) TRUE, (char *) &G.clobber,
     "Overwrite output files"},
    {"-list", ARGV_CONSTANT, (char *) TRUE, (char *) &G.List,
     "Print list of series (don't create files)"},
    {"-anon", ARGV_CONSTANT, (char *) TRUE, (char *) &G.Anon,
     "Exclude subject name from file header"},
    {"-descr", ARGV_STRING, (char *) 1, G.Name,
     "Use <str> as session descriptor (default = patient initials)"},
    {"-cmd", ARGV_STRING, (char *) 1, G.command_line, 
     "Apply <command> to output files (e.g. gzip)"},
    {"-verbose", ARGV_CONSTANT, (char *) 1, (char *) &G.Debug,
     "Print debugging information"},
    {"-debug", ARGV_CONSTANT, (char *) 2, (char *) &G.Debug,
     "Print lots of debugging information"},
    {"-nosplitecho", ARGV_CONSTANT, (char *) FALSE, (char *) &G.splitEcho,
     "Combine all echoes into a single file."},
    {"-splitdynamic", ARGV_CONSTANT, (char *) TRUE, (char *)&G.splitDynScan,
     "Split dynamic scans into a separate files."},
    {"-opts", ARGV_INT, (char *) 1, (char *) &G.opts, 
     "Set debugging options"},

    {"-descending", 
     ARGV_CONSTANT, 
     (char *) MOSAIC_SEQ_DESCENDING, 
     (char *) &G.mosaic_seq,
     "Mosaic sequence is in descending slice order."},

    {"-interleaved", 
     ARGV_CONSTANT, 
     (char *) MOSAIC_SEQ_INTERLEAVED, 
     (char *) &G.mosaic_seq,
     "Mosaic sequence is in interleaved slice order."},
    {NULL, ARGV_END, NULL, NULL, NULL}
};

int 
main(int argc, char *argv[])
{
    int ifile;
    Acr_Group group_list;
    const char **file_list;     /* List of file names */
    Data_Object_Info **file_info_list;
    int num_files;              /* Total number of files on command line */
    string_t out_dir;           /* Output directory */
    string_t message;           /* Generic message */
    int num_files_ok;           /* Actual number of DICOM/IMA files */

    G.mosaic_seq = MOSAIC_SEQ_ASCENDING; /* Assume ascending by default. */
    G.splitDynScan = FALSE;     /* Don't split dynamic scans by default */
    G.splitEcho = TRUE;         /* Do split by echo by default */

    G.minc_history = time_stamp(argc, argv); /* Create minc history string */

    G.pname = argv[0];          /* get program name */
    
    /* Get the input parameters and file names.
     */
    if (ParseArgv(&argc, argv, argTable, 0)) {
        usage();
    }

    if (argc < 2) {
        usage();
    }

    if (G.List) {
        num_files = argc - 1;   /* Assume no directory given. */
    }
    else {
        struct stat st;
        int n;

        num_files = argc - 2;   /* Assume last arg is directory. */

        strcpy(out_dir, argv[argc - 1]); 

        /* make sure path ends with slash 
         */
        n = strlen(out_dir);
        if (out_dir[n - 1] != '/') {
            out_dir[n++] = '/';
            out_dir[n++] = '\0';
        }

        if (stat(out_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "The final argument, '%s', is not a directory\n", 
                    out_dir);
            exit(EXIT_FAILURE);
        }
    }

    /* Get space for file lists */
    /* Allocate the array of pointers used to implement the
     * list of filenames.
     */
    file_list = malloc(num_files * sizeof(*file_list));
    CHKMEM(file_list);

    for (ifile = 0 ; ifile < num_files; ifile++) {
        file_list[ifile] = strdup(argv[ifile + 1]);
    }

    file_info_list = malloc(num_files * sizeof(*file_info_list));
    CHKMEM(file_info_list);

    /* figure out what kind of files we have -
     * supported types are:
     *
     *  IMA (Siemens .ima format - Numaris 3.5)
     *  N4DCM (Siemens DICOM - Numaris 4)
     *
     * if not all same type, return an error 
     *
     * we start by assuming N4DCM with no offset - we find that the
     * file is IMA or has an offset (the 128 byte + DICM offset seen
     * on Syngo CD's and exports) then the appropriate flag will be
     * set.
     */

    printf("Checking file types...   ");

    if (check_file_type_consistency(num_files, file_list) < 0) {
        exit(EXIT_FAILURE);
    }

    /* Now loop over all files, getting basic info on each
     */

    num_files_ok = 0;
    for (ifile = 0; ifile < num_files; ifile++) {
        const char *cur_fname_ptr = file_list[ifile];

        if (!G.Debug) {
            sprintf(message, "Parsing %d files", num_files);
            progress(ifile, num_files, message);
        }

        if (G.file_type == IMA) {
            group_list = siemens_to_dicom(cur_fname_ptr, TRUE);
        }
        else {
            /* read up to but not including pixel data
             */
            group_list = read_numa4_dicom(cur_fname_ptr, ACR_IMAGE_GID - 1);
        } 

        if (group_list == NULL) {
            /* This file appears to be invalid - it is probably a dicomdir
             * file or some other stray junk in the directory.
             */
            printf("Skipping file %s, which is not in the expected format.\n",
                   cur_fname_ptr);
            free((void *) cur_fname_ptr);
        }
        else {
            /* Copy it back to the (possibly earlier) position in the real
             * file list.
             */
            file_list[num_files_ok] = cur_fname_ptr;

            /* allocate space for the current entry to file_info_list 
             */
            file_info_list[num_files_ok] = malloc(sizeof(*file_info_list[0]));
            CHKMEM(file_info_list[num_files_ok]);
            file_info_list[num_files_ok]->file_index = num_files_ok;

            parse_dicom_groups(group_list, file_info_list[num_files_ok]);

            /* put the file name into the info list
             */
            file_info_list[num_files_ok]->file_name = strdup(file_list[num_files_ok]);

            /* Delete the group list now that we're done with it
             */
            acr_delete_group_list(group_list);
            num_files_ok++;
        }
    } /* end of loop over files to get basic info */

    if (G.Debug) {
        printf("Using %d files\n", num_files_ok);
    }

    num_files = num_files_ok;

    printf("Sorting %d files...   ", num_files);

    if (G.file_type == N4DCM) {
        /* sort the files into series based on acquisition number
         */
        qsort(file_info_list, num_files, sizeof(file_info_list[0]),
              dcm_sort_function);

    } 
    else if (G.file_type == IMA) {
        /* sort the files based on file name
         * (could also use dcm_sort_function, but would have 
         * to use ACR_Image instead of ACR_Acquisition
         */
        qsort(file_list, num_files, sizeof(file_list[0]), ima_sort_function);
    }

    /* If DEBUG, print a list of all files.
     */
    if (G.Debug) {
        printf("\n");
        for (ifile = 0; ifile < num_files; ifile++) {
            Data_Object_Info *info = file_info_list[ifile];
            char *fname;

            if ((ifile % 16) == 0) {
                printf("%-4s %-32.32s %-8s %-6s %-8s %-8s %-4s %-4s %-4s %-4s %-4s %-4s %-4s %-4s %-4s %-5s %-16s\n",
                       "num",
                       "filename",
                       "date",
                       "time",
                       "serialno",
                       "acq",
                       "nec",
                       "iec",
                       "ndy",
                       "idy",
                       "nsl",
                       "isl",
                       "acol",
                       "rcol",
                       "mrow",
                       "img#",
                       "seq");
            }
            /* Print out info about file.  Truncate the name if necessary.
             */
            fname = info->file_name;
            if (strlen(fname) > 32) {
                fname += strlen(fname) - 32;
            }

            printf("%4d %-32.32s %8d %6d %8d %8d %4d %4d %4d %4d %4d %4d %4d %4d %4d %5d %-16s\n",
                   ifile,
                   fname,
                   info->study_date,
                   info->study_time,
                   info->scanner_serialno,
                   info->acq_id,
                   info->num_echoes,
                   info->echo_number,
                   info->num_dyn_scans,
                   info->dyn_scan_number,
                   info->num_slices_nominal,
                   info->slice_number,
                   info->acq_cols,
                   info->rec_cols,
                   info->num_mosaic_rows,
                   info->global_image_number,
                   info->sequence_name);
        }
    }

    printf("Done sorting files.\n");

    /* Loop over files, processing by acquisition */ 

    if (G.List) {
        printf("Listing files by series...\n");
    }
    else {
        printf("Processing files, one series at a time...\n");
    }

    use_the_files(num_files, file_list, file_info_list, out_dir);

    if (G.List) {
        printf("Done listing files.\n");
    }
    else {
        printf("Done processing files.\n");
    }

    free_list(num_files, file_list, file_info_list);

    free(file_list);
    free(file_info_list);

    
    exit(EXIT_SUCCESS);
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : free_list
@INPUT      : num_files - number of files in list
              file_list - array of file names
@OUTPUT     : (none)
@RETURNS    : (nothing)
@DESCRIPTION: Frees up things pointed to in pointer arrays. Does not free
              the arrays themselves.
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : November 22, 1993 (Peter Neelin)
@MODIFIED   : 
---------------------------------------------------------------------------- */
static void 
free_list(int num_files, 
          const char **file_list, 
          Data_Object_Info **file_info_list)
{
    int i;

    for (i = 0; i < num_files; i++) {
        if (file_list[i] != NULL) {
            free((void *) file_list[i]);
        }
        if (file_info_list[i] != NULL) {
            free(file_info_list[i]);
        }
    }
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : ima_sort_function
@INPUT      : entry1
              entry2
@OUTPUT     : (none)
@RETURNS    : -1, 0, 1 for lt, eq, gt
@DESCRIPTION: Function to compare two ima file names
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June 2001 (Rick Hoge)
@MODIFIED   : 
---------------------------------------------------------------------------- */
static int
ima_sort_function(const void *entry1, const void *entry2)
{
    char * const *value1 = entry1;
    char * const *value2 = entry2;

    int session1,series1,image1;
    int session2,series2,image2;
  
    sscanf(*value1,"%d-%d-%d.ima",&session1,&series1,&image1);
    sscanf(*value2,"%d-%d-%d.ima",&session2,&series2,&image2);
  
    if (session1 < session2) return -1;
    else if (session1 > session2) return 1;
    else if (series1 < series2) return -1;
    else if (series1 > series2) return 1;
    else if (image1 < image2) return -1;
    else if (image1 > image2) return 1;
    else return 0;
}

/* ----------------------------- MNI Header -----------------------------------
@NAME       : dcm_sort_function
@INPUT      : entry1
              entry2
@OUTPUT     : (none)
@RETURNS    : -1, 0, 1 for lt, eq, gt
@DESCRIPTION: Function to compare two dcm series numbers
@METHOD     : 
@GLOBALS    : 
@CALLS      : 
@CREATED    : June 2001 (Rick Hoge)
@MODIFIED   : 
---------------------------------------------------------------------------- */
static int 
dcm_sort_function(const void *entry1, const void *entry2)
{
    Data_Object_Info **file_info_list1 = (Data_Object_Info **) entry1;
    Data_Object_Info **file_info_list2 = (Data_Object_Info **) entry2;

    // make a sort-able session ID number:  date.time
    double session1 = (*file_info_list1)->study_date +
        (*file_info_list1)->study_time / 1e6;
    double session2 = (*file_info_list2)->study_date +
        (*file_info_list2)->study_time / 1e6;

    // series index
    int series1 = (*file_info_list1)->acq_id;
    int series2 = (*file_info_list2)->acq_id;

    // frame index
    int frame1 = (*file_info_list1)->dyn_scan_number;
    int frame2 = (*file_info_list2)->dyn_scan_number;

    // image index
    int image1 = (*file_info_list1)->global_image_number;
    int image2 = (*file_info_list2)->global_image_number;

    if (session1 < session2) return -1;
    else if (session1 > session2) return 1;
    else if (series1 < series2) return -1;
    else if (series1 > series2) return 1;
    else if (frame1 < frame2) return -1;
    else if (frame1 > frame2) return 1;
    else if (image1 < image2) return -1;
    else if (image1 > image2) return 1;
    else return 0;
}

static void 
usage(void)
{
    fprintf(stderr, "\nUsage: %s [options] file1 file2 file3 ... destdir\n",
            G.pname);
    fprintf(stderr, "\n");
    fprintf(stderr,"Files are named according to the following convention:\n\n");
    fprintf(stderr,"  Directory: lastname_firstname_yyyymmdd_hhmmss/\n");
    fprintf(stderr,"  Files:     lastname_firstname_yyyymmdd_hhmmss_series_modality.mnc\n\n");

    exit(EXIT_FAILURE);
}

static void
use_the_files(int num_files, 
              const char *file_list[], 
              Data_Object_Info *data_info[],
              const char *out_dir)
{
    int ifile;
    int acq_num_files;
    const char **acq_file_list;
    int *used_file;
    double cur_study_id;
    int cur_acq_id;
    int cur_rec_num;
    int cur_image_type;
    int cur_echo_number;
    int cur_dyn_scan_number;
    int exit_status;
    char *output_file_name;
    string_t file_prefix;
    int output_uid;
    int output_gid;
    string_t string;
    FILE *fp;

    if (out_dir != NULL) {    /* if an output directory name has been 
                               * provided on the command line
                               */
        if (G.Debug) {
            printf("Using directory '%s'\n", out_dir);
        }
        strcpy(file_prefix, out_dir);
    }
    else {
        file_prefix[0] = '\0';
    }

    if (G.Debug) {                /* debugging */
        printf("file_prefix:  [%s]\n", file_prefix);
    }

    /* Allocate space for acquisition file list.
     */
    acq_file_list = malloc(num_files * sizeof(*acq_file_list));
    CHKMEM(acq_file_list);

    used_file = malloc(num_files * sizeof(*used_file));
    CHKMEM(used_file);

    for (ifile = 0; ifile < num_files; ifile++) {
        used_file[ifile] = FALSE;
    }

    for (;;) {

        /* Loop through files, looking for an acquisition
         * 
         * file groups should already have been sorted into acquisitions
         * in calling program 
         *
         * this code is in a `forever' loop because we loop over multiple
         * acquisitions until all of the files are used up.
         */

        acq_num_files = 0;

        for (ifile = 0; ifile < num_files; ifile++) {

            /* If already marked used (can this happen???), we've already
             * written the file to an output somewhere.
             */
            if (used_file[ifile]) {
                continue;
            }

            if (acq_num_files == 0) {
	 
                /* found first file: set all current attributes like
                 * study id, acq id, rec num(?), image type, echo
                 * number, dyn scan number, flag for multiple echoes,
                 * flag for multiple time points the flag input file
                 * as `used' 
                 */ 
	 
                cur_study_id = data_info[ifile]->study_id;
                cur_acq_id = data_info[ifile]->acq_id;
                cur_rec_num = data_info[ifile]->rec_num;
                cur_image_type = data_info[ifile]->image_type;
                cur_echo_number = data_info[ifile]->echo_number;
                cur_dyn_scan_number = data_info[ifile]->dyn_scan_number;

                used_file[ifile] = TRUE;
            }
            /* otherwise check if attributes of the new input file match those
             * of the current output context and flag input file as `used' 
             */
            else if ((data_info[ifile]->study_id == cur_study_id) &&
                     (data_info[ifile]->acq_id == cur_acq_id) &&
                     (data_info[ifile]->rec_num == cur_rec_num) &&
                     (data_info[ifile]->image_type == cur_image_type) &&
                     (data_info[ifile]->echo_number == cur_echo_number ||
                      !G.splitEcho) &&
                     (data_info[ifile]->dyn_scan_number == cur_dyn_scan_number ||
                      !G.splitDynScan)) {

                used_file[ifile] = TRUE;
            }
            if (used_file[ifile]) {
	 
                /* if input file is flagged as `used', then add its index
                   to the list of files for this acquisition (and increment
                   counter) */

                acq_file_list[acq_num_files] = file_list[ifile];
                acq_num_files++;
            }
        }
     
        if (acq_num_files == 0) {
            break;              /* All done!!! */
        }
       
        /* Use the files for this acquisition
         */
     
        /* Print out the file names if we are debugging.
         */
        if (G.Debug) {
            printf("\nFiles copied:\n");
            for (ifile = 0; ifile < acq_num_files; ifile++) {
                printf("     %s\n", acq_file_list[ifile]);
            }
        }

        /* Create minc file
         */
        exit_status = dicom_to_minc(acq_num_files, 
                                    acq_file_list, 
                                    NULL,
                                    G.clobber, 
                                    file_prefix, 
                                    &output_file_name);

        if (exit_status != EXIT_SUCCESS) 
            continue;
       
        /* Print log message */
        if (G.Debug) {
            printf("Created minc file %s.\n", output_file_name);
        }
       
        /* Invoke a command on the file (if requested) and get the 
         * returned file name 
         */
        if (G.command_line[0] != '\0') {
            sprintf(string, "%s %s", G.command_line, output_file_name);
            printf("-Applying command '%s' to output file...  ", 
                   G.command_line);
            fflush(stdout);
            if ((fp = popen(string, "r")) != NULL) {
                fscanf(fp, "%s", output_file_name);
                if (pclose(fp) != EXIT_SUCCESS) {
                    fprintf(stderr, 
                            "Error executing command\n   \"%s\"\n",
                            string);
                }
                else if (G.Debug) {
                    printf("Executed command \"%s\",\nproducing file %s.\n",
                           string, output_file_name);
                }
            }
            else {
                fprintf(stderr, "Error executing command \"%s\"\n", string);
            }
            printf("Done.\n");
        }
       
        /* Change the ownership */
        if ((output_uid != INT_MIN) && (output_gid != INT_MIN)) {
            chown(output_file_name, (uid_t) output_uid, (gid_t) output_gid);
        }
    }
   
    /* Free acquisition file list */
    free(acq_file_list);
    free(used_file);

}

#define DICM_MAGIC_SIZE 4
#define DICM_MAGIC_OFFS 128

static int
check_file_type_consistency(int num_files, const char *file_list[])
{
    int i;
    FILE *fp;
    char dicm_test_string[DICM_MAGIC_SIZE];
    const char *fn_ptr;
    int n4_offset = 0;

    for (i = 0; i < num_files; i++) {

        fn_ptr = file_list[i];

        if ((fp = fopen(fn_ptr, "rb")) == NULL) {
            fprintf(stderr, "Error opening file %s!\n", fn_ptr);
            return (-1);
        }

        /* Numaris 4 DICOM CD/Export file? if so, bytes 129-132 will 
         * contain the string `DICM' 
         */

        fseek(fp, DICM_MAGIC_OFFS, SEEK_SET);
        fread(dicm_test_string, 1, DICM_MAGIC_SIZE, fp);

        if (dicm_test_string[0] == 'D' && dicm_test_string[1] == 'I' &&
            dicm_test_string[2] == 'C' && dicm_test_string[3] == 'M') {
            if (G.file_type == UNDEF) {
                G.file_type = N4DCM;
                n4_offset = 1; 
                printf("assuming files are Syngo DICOM (CD/Export).\n");
            }
            else if (G.file_type != N4DCM || n4_offset != 1) {
                printf("file type mismatch detected\n");
                return (-1);
            }
        } 
        else if (!strcmp(fn_ptr + strlen(fn_ptr) - 4, ".ima")) {
            if (G.file_type == UNDEF) {
                G.file_type = IMA;
                printf("assuming files are IMA.\n");
            }
            else if (G.file_type != IMA) {
                printf("file type mismatch detected\n");
                return (-1);
            }
        }
        else {
            if (G.file_type == UNDEF) {
                G.file_type = N4DCM;
                n4_offset = 0; 
                printf("assuming files are standard DICOM.\n");
            }
            else if (G.file_type != N4DCM || n4_offset != 0) {
                printf("file type mismatch detected\n");
            }
        }
        fclose(fp);
    }
    return (0);
}
