#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <jansson.h>

#define SSM_BUFFER_SIZE (2 * 1024)  /**< 2000 KB buffer size */
#define SSM_STR_BUFFSIZE 255 /**< buffer for log and error strings */

typedef enum {SSM_SMC = 1 << 0, SSM_MIF = 1 << 1, SSM_PMCMC = 1 << 2, SSM_KMCMC = 1 << 3, SSM_KALMAN = 1 << 4, SSM_KSIMPLEX = 1 << 5, SSM_SIMUL = 1 << 6, SSM_SIMPLEX = 1 << 7, SSM_WORKER = 1 << 8 } ssm_algo_t;

typedef enum {SSM_ODE, SSM_SDE, SSM_PSR, SSM_EKF} ssm_implementations_t;
typedef enum {SSM_NO_DEM_STO = 1 << 0, SSM_NO_WHITE_NOISE = 1 << 1, SSM_NO_DIFF = 1 << 2 } ssm_noises_off_t; //several noises can be turned off

typedef enum {SSM_PRINT_TRACE = 1 << 0, SSM_PRINT_X = 1 << 1, SSM_PRINT_HAT = 1 << 2, SSM_PRINT_PRED_RES = 1 << 3, SSM_PRINT_X_SMOOTH = 1 << 4, SSM_PRINT_ACC = 1 << 5, SSM_PIPE = 1 << 6, SSM_QUIET = 1 << 7, SSM_PRINT_COV = 1 << 8 } ssm_print_t;



void ssm_print_log(char *msg)
{
    json_t *root;
    root = json_pack("{s,s,s,s}", "flag", "log", "msg", msg);
    json_dumpf(root, stdout, 0); printf("\n");
    fflush(stdout);
    json_decref(root);
}


void ssm_print_err(char *msg)
{
    json_t *root;
    root = json_pack("{s,s,s,s}", "flag", "err", "msg", msg);
    json_dumpf(root, stderr, 0); fprintf(stderr,"\n");
    fflush(stderr);
    json_decref(root);
}


char *ssm_c1_new(int n)
{
    char *tab = malloc(n*sizeof (char));

    if(tab==NULL) {
	char str[SSM_STR_BUFFSIZE];
	sprintf(str, "Allocation impossible in file :%s line : %d",__FILE__,__LINE__);
	ssm_print_err(str);
	exit(EXIT_FAILURE);
    }

    return tab;
}



/**
 * options
 */
typedef struct
{
    ssm_implementations_t implementation;
    ssm_noises_off_t noises_off;
    ssm_print_t print;

    int id;                  /**< unique integer identifier that will be used as seed and be appended to the output files */
    int flag_seed_time;      /**< seed with the local time ((unsigned) time(NULL)) */
    int flag_pipe;           /**< pipe mode */
    int flag_prior;          /**< add log(prior) to the estimated log likelihood */
    double dt;               /**< integration time step in days */
    double eps_abs;          /**< absolute error for adaptive step-size control */
    double eps_rel;          /**< relative error for adaptive step-size control */
    char *freeze_forcing;    /**< freeze the metadata to their value at the specified ISO8601 date */
    char *path;              /**< path where the outputs will be stored */
    int n_thread;            /**< number of threads */
    double like_min;         /**< particles with likelihood smaller that like_min are considered lost */
    int J;                   /**< number of particles */
    int n_obs;               /**< number of observations to be fitted (for tempering) */
    char *interpolator;      /**< gsl interpolator for metadata */
    int n_iter;              /**< number of iterations */
    double a;                /**< cooling factor (scales standard deviation) */
    double b;                /**< re-heating (inflation) (scales standard deviation of the proposal) */
    double L;                /**< lag for fixed lag smoothing (proportion of the data) */
    int m_switch;            /**< iteration number when we switch (for initial covariance to empirical or from different update formulae) */
    int flag_ic_only;        /**< only fit the initial condition using fixed lag smoothing */
    int eps_switch;          /**< select number of burnin iterations before tuning epsilon */
    double eps_max;          /**< maximum value allowed for epsilon */
    double flag_smooth;      /**< tune epsilon with the value of the acceptance rate obtained with exponential smoothing */
    double alpha;            /**< smoothing factor of exponential smoothing used to compute the smoothed acceptance rate (low values increase degree of smoothing) */
    int n_traj;              /**< number of trajectories stored */
    int flag_zmq;            /**< dispatch particles across machines using a zeromq pipeline */
    int chunk;               /**< number of particles to send to each machine */
    int flag_least_squares;  /**< optimize the sum of square instead of the likelihood */
    int size_stop;           /**< simplex size used as a stopping criteria */
    char *freq;             /**< print the outputs (and reset incidences to 0 if any) every day (D), week (W), bi-week (B), month (M  or year (Y) */
    char *start;             /**< ISO 8601 date when the simulation starts*/
    char *end;               /**< ISO 8601 date when the simulation ends*/
    int skip;                /**< number of days to skip (used to skip transient dynamics) */
    char *server;            /**< domain name or IP address of the particule server (e.g 127.0.0.1) */
    int flag_no_filter;      /**< do not filter */      
} ssm_options_t;


struct opts_part{
    char *s;
    int val;
    char *l;
    char *description;
    int has_arg;
    ssm_algo_t algo;
};



ssm_options_t *ssm_options_new(void)
{
    ssm_options_t *opts = malloc(sizeof(ssm_options_t));

    //alloc char *
    opts->freeze_forcing = ssm_c1_new(SSM_STR_BUFFSIZE);
    opts->path = ssm_c1_new(SSM_STR_BUFFSIZE);
    opts->interpolator = ssm_c1_new(SSM_STR_BUFFSIZE);
    opts->freq = ssm_c1_new(SSM_STR_BUFFSIZE);
    opts->start = ssm_c1_new(SSM_STR_BUFFSIZE);
    opts->end = ssm_c1_new(SSM_STR_BUFFSIZE);
    opts->server = ssm_c1_new(SSM_STR_BUFFSIZE);

    //fill default
    opts->implementation;
    opts->noises_off;
    opts->print;

    opts->id = 0;
    opts->flag_seed_time = 0;
    opts->flag_pipe = 0;
    opts->flag_prior = 0;
    opts->dt = -1.0;
    opts->eps_abs = 1e-6;
    opts->eps_rel = 1e-3;
    strncpy(opts->freeze_forcing, "", SSM_STR_BUFFSIZE);
    strncpy(opts->path, "./", SSM_STR_BUFFSIZE);
    opts->n_thread = 1;
    opts->like_min = 1e-17;
    opts->J = 1;
    opts->n_obs = -1;
    strncpy(opts->interpolator, "linear", SSM_STR_BUFFSIZE);
    opts->n_obs = -1;
    opts->n_iter = 10;
    opts->a = 0.98;
    opts->b = 2;
    opts->L = 0.75;
    opts->m_switch = -1;
    opts->flag_ic_only = 0;
    opts->eps_switch = 50;
    opts->eps_max = 50.0;
    opts->flag_smooth = 0;
    opts->alpha = 0.02;
    opts->n_traj = 1000;
    opts->flag_zmq = 0;
    opts->chunk = 0;
    opts->flag_least_squares = 0;
    opts->size_stop = 1e-6;
    strncpy(opts->freq, "D", SSM_STR_BUFFSIZE);
    strncpy(opts->start, "", SSM_STR_BUFFSIZE);
    strncpy(opts->end, "", SSM_STR_BUFFSIZE);
    opts->skip = 0;
    strncpy(opts->server, "127.0.0.1", SSM_STR_BUFFSIZE);
    opts->flag_no_filter = 0;

    return opts;
}


ssm_options_free(ssm_options_t *opts)
{
    free(opts->freeze_forcing);
    free(opts->path);
    free(opts->interpolator);
    free(opts->freq);
    free(opts->start);
    free(opts->end);
    free(opts->server);

    free(opts);
}


void get_opts(ssm_options_t *opts, ssm_algo_t algo, int argc, char *argv[])
{
    struct opts_part all_opts[] = {
        {"D", 'D', "dt",             "integration time step", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"I", 'I', "id",             "general id (unique integer identifier that will be appended to the output)", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"P", 'P', "path",           "root path to access data", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"N", 'N', "n_thread",       "number of threads to be used", required_argument,  SSM_SMC | SSM_PMCMC | SSM_MIF | SSM_SIMUL },
        {"J", 'J', "n_parts",        "number of particles", required_argument,  SSM_SMC | SSM_PMCMC | SSM_MIF | SSM_SIMUL },
        {"O", 'O', "n_obs",          "number of observations to be fitted (for tempering)", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF },
        {"A", 'A', "cooling",        "cooling factor (for sampling covariance live tuning or MIF cooling)", required_argument, SSM_KMCMC | SSM_PMCMC | SSM_MIF },
        {"C", 'C', "switch",         "select switching iteration from initial covariance to empirical one (mcmc) or to update formula introduced in Ionides et al. 2006 (mif)", required_argument,  SSM_KMCMC | SSM_PMCMC },
        {"W", 'W', "eps_switch",     "select number of burnin iterations before tuning epsilon", required_argument,  SSM_KMCMC | SSM_PMCMC },
        {"T", 'T', "n_traj",         "number of trajectories stored", required_argument,  SSM_KMCMC | SSM_PMCMC },
        {"M", 'M', "iter",           "number of iterations", required_argument,  SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF },
        {"B", 'B', "start",          "ISO 8601 date when simulation starts", required_argument,  SSM_SIMUL },
        {"E", 'E', "end",            "ISO 8601 date when simulation end", required_argument,  SSM_SIMUL },
        {"Y", 'Y', "eps_abs_integ",  "absolute error for adaptive step-size control", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"Z", 'Z', "eps_rel_integ",  "relative error for adaptive step-size control", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"G", 'G', "freeze_forcing", "freeze covariates to their value at specified ISO 8601 date", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"K", 'K', "like_min",       "if applicable, particles with likelihood smaller than like_min are considered lost. Otherwise, lower bound on likelihood", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF },
        {"U", 'U', "eps_max",        "maximum value allowed for epislon", required_argument,  SSM_KMCMC | SSM_PMCMC },
        {"S", 'S', "alpha",          "smoothing factor of exponential smoothing used to compute smoothed acceptance rate (low values increase degree of smoothing)", required_argument,  SSM_KMCMC | SSM_PMCMC },
        {"H", 'H', "heat",           "re-heating accross MIF iterations (scales standard deviation of proposals)", required_argument,  SSM_MIF },
        {"L", 'L', "lag",            "lag for fixed-lag smoothing (proportion of the data)", required_argument,  SSM_MIF },
        {"F", 'F', "freq",           "print the outputs (and reset incidences to 0 if any) every day (D), week (W), bi-week (B), month (M) or year (Y)", required_argument,  SSM_SIMUL },
        {"V", 'V', "size",           "simplex size used as stopping criteria", required_argument,  SSM_KSIMPLEX | SSM_SIMPLEX },
        {"X", 'X', "chunk",          "number of particles sent to each machine", required_argument,  SSM_PMCMC },
        {"R", 'R', "skip",           "number of days to skip (used to skip transient dynamics)", required_argument,  SSM_SIMUL },
        {"Q", 'Q', "interpolator",   "gsl interpolator for covariates", required_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },

        {"i", 'i', "server",         "domain name or IP address of the particule server (e.g 127.0.0.1)", required_argument,  SSM_WORKER }, //not enough capital letter :(


        {"h", 'h', "help",           "print the usage on stdout", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"q", 'q', "quiet",          "no verbosity", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"d", 'd', "no_dem_sto",     "turn off demographic stochasticity  (if any)", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"w", 'w', "no_white_noise", "turn off white noises (if any)", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"f", 'f', "no_diff",        "turn off diffusions (if any)", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL },
        {"t", 't', "traj",           "print the trajectories", no_argument,  SSM_SMC | SSM_KALMAN | SSM_MIF | SSM_SIMUL },
        {"r", 'r', "no_filter",      "do not filter", no_argument,  SSM_SMC },
        {"c", 'c', "trace",          "print the traces", no_argument,  SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF },
        {"x", 'x', "hat",            "print the state estimates", no_argument,  SSM_SMC | SSM_KALMAN | SSM_SIMUL },
        {"e", 'e', "pred_res",       "print the prediction residuals", no_argument,  SSM_SMC | SSM_KALMAN },
        {"p", 'p', "prior",          "add log(prior) to the estimated loglikelihood", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF },
        {"s", 's', "smooth",         "tune epsilon with the value of the acceptance rate obtained with exponential smoothing", no_argument,  SSM_KMCMC | SSM_PMCMC },
        {"a", 'a', "acc",            "print the acceptance rate", no_argument,  SSM_KMCMC | SSM_PMCMC },
        {"z", 'z', "zmq",            "dispatch particles across machines using a zmq pipeline", no_argument,  SSM_PMCMC },
        {"b", 'b', "ic_only",        "only fit the initial condition using fixed lag smoothing", no_argument,  SSM_MIF },
        {"l", 'l', "least_squares",  "minimize the sum of squared errors instead of maximizing the likelihood", no_argument,  SSM_SIMPLEX },
        {"g", 'g', "seed_time",      "seed the random number generator with the current time", no_argument,  SSM_SMC | SSM_KALMAN | SSM_KMCMC | SSM_PMCMC | SSM_KSIMPLEX | SSM_SIMPLEX | SSM_MIF | SSM_SIMUL }
    };

    int i;
    int n_all_opts = sizeof(all_opts)/sizeof(*all_opts);
    int n_opts = 0;
    for(i=0; i< n_all_opts; i++){
        if(all_opts[i].algo & algo) n_opts++;
    }

    char shortopts[SSM_STR_BUFFSIZE] = "";
    char help_msg[SSM_BUFFER_SIZE] = "";
    struct option long_options[n_opts+1];

    int j=0;
    for(i=0; i< n_all_opts; i++){
        if(all_opts[i].algo & algo){
            strcat(shortopts, all_opts[i].s);
            if(all_opts[i].has_arg == required_argument){
                strcat(shortopts, ":");
            }

            snprintf(help_msg, SSM_BUFFER_SIZE, "%s-%s, --%-20s %s\n", help_msg, all_opts[i].s, all_opts[i].l, all_opts[i].description);

            long_options[j].name = all_opts[i].l;
            long_options[j].has_arg = all_opts[i].has_arg;
            long_options[j].flag = NULL;
            long_options[j].val = all_opts[i].val;
            j++;
        }
    }

    long_options[n_opts].name = NULL;
    long_options[n_opts].has_arg = 0;
    long_options[n_opts].flag = NULL;
    long_options[n_opts].val = 0;

    int c;
    char str[SSM_STR_BUFFSIZE];

    while (1) {
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long (argc, argv, shortopts, long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1) break;
        switch (c) {
        case 0:
            /* If this option set a flag, do nothing else now. */
            if (long_options[option_index].flag != 0) {
                break;
            }
            break;

        case 'h':
            ssm_print_log(help_msg);
            exit(EXIT_SUCCESS);


        case 'D': //dt
	    opts->dt = atof(optarg);
            break;

        case 'I': //id
	    opts->id = atoi(optarg);
            break;

        case 'P': //path
	    strncpy(opts->path, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'N': //n_thread
	    opts->n_thread = atoi(optarg);
            break;

        case 'J': //n_parts
	    opts->J = atoi(optarg);
            break;

        case 'O': //n_obs
	    opts->n_obs = atoi(optarg);
            break;

        case 'A': //cooling
	    opts->a = atof(optarg);
            break;

        case 'C': //switch
	    opts->m_switch = atoi(optarg);
            break;

        case 'W': //eps_switch
	    opts->eps_switch = atoi(optarg);
            break;

        case 'T': //n_traj
	    opts->n_traj = atoi(optarg);
            break;

        case 'M': //iter
	    opts->n_iter = atoi(optarg);
            break;

        case 'B': //start
	    strncpy(opts->start, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'E': //end
	    strncpy(opts->end, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'Y': //eps_abs_integ
	    opts->eps_abs = atof(optarg);
            break;

        case 'Z': //eps_rel_integ
	    opts->eps_rel = atof(optarg);
            break;

        case 'G': //freeze_forcing
	    strncpy(opts->freeze_forcing, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'K': //like_min
	    opts->like_min = atof(optarg);
            break;

        case 'U': //eps_max
	    opts->eps_max = atof(optarg);
            break;

        case 'S': //alpha
	    opts->alpha = atof(optarg);
            break;

        case 'H': //heat
	    opts->b = atof(optarg);
            break;

        case 'L': //lag
	    opts->L = atof(optarg);
            break;

        case 'F': //freq
	    strncpy(opts->freq, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'V': //size
	    opts->size_stop = atoi(optarg);	   
            break;

        case 'X': //chunk
	    opts->chunk = atoi(optarg);	   
            break;

        case 'R': //skip
	    opts->skip = atoi(optarg);	   	    
            break;

        case 'Q': //interpolator
	    strncpy(opts->interpolator, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'i': //server
	    strncpy(opts->server, optarg, SSM_STR_BUFFSIZE);
            break;

        case 'q': //quiet
	    opts->print |= SSM_QUIET;
            break;

        case 'd': //no_dem_sto
	    opts->noises_off |= SSM_NO_DEM_STO;
            break;

        case 'w': //no_white_noise
	    opts->noises_off |= SSM_NO_WHITE_NOISE;
            break;

        case 'f': //no_diff
	    opts->noises_off |= SSM_NO_DIFF;
            break;

        case 't': //traj
	    opts->print |= SSM_PRINT_X;
            break;

        case 'r': //no_filter
	    opts->flag_no_filter = 1;
            break;

        case 'c': //trace
	    opts->print |= SSM_PRINT_TRACE;
            break;

        case 'x': //hat
	    opts->print |= SSM_PRINT_HAT;
            break;

        case 'e': //pred_res
	    opts->print |= SSM_PRINT_PRED_RES;
            break;

        case 'p': //prior
	    opts->flag_prior = 1;
            break;

        case 's': //smooth
	    opts->flag_smooth = 1;
            break;

        case 'a': //acc
	    opts->print |= SSM_PRINT_ACC;
            break;

        case 'z': //zmq
	    opts->flag_zmq = 1;
            break;

        case 'b': //ic_only
	    opts->flag_ic_only = 1;
            break;

        case 'l': //least_squares
	    opts->flag_least_squares = 1;	    
            break;

	case 'g': //seed_time"
	    opts->flag_seed_time = 1;	    
	    break;

        case '?':
            exit(EXIT_FAILURE);

        default:
	    abort();
        }

    }

    argc -= optind;
    argv += optind;

    if(argc > 0) {
        printf("argv: %s\n", argv[0]);
    }
}


int main(int argc, char *argv[])
{
    ssm_options_t *opts = ssm_options_new();
    
    get_opts(opts, SSM_MIF, argc, argv);
    printf("cooling: %g\n", opts->a);

    ssm_options_free(opts);

    return 0;
}