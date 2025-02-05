/**
 * An example of benchmarking random search on a COCO suite. A grid search
 * optimizer is also implemented and can be used instead of random search.
 *
 * Set the global parameter BUDGET_MULTIPLIER to suit your needs.
 */
#include "ecs.h"

/**
 * The maximal budget for evaluations done by an optimization algorithm equals
 * dimension * BUDGET_MULTIPLIER. Increase the budget multiplier value gradually
 * to see how it affects the runtime.
 */
static const unsigned int BUDGET_MULTIPLIER = 100;

/**
 * The maximal number of independent restarts allowed for an algorithm that
 * restarts itself.
 */
static const long INDEPENDENT_RESTARTS = 1e5;

/**
 * The random seed. Change if needed.
 */
static const int32_t RANDOM_SEED = 0xdeadbeef;

/**
 * A function type for evaluation functions, where the first argument is the
 * vector to be evaluated and the second argument the vector to which the
 * evaluation result is stored.
 */
typedef void (*evaluate_function_t)(const double *x, double *y);

/**
 * A pointer to the problem to be optimized (needed in order to simplify the
 * interface between the optimization algorithm and the COCO platform).
 */
static coco_problem_t *PROBLEM;

/**
 * Calls coco_evaluate_function() to evaluate the objective function
 * of the problem at the point x and stores the result in the vector y
 */
static void evaluate_function(const double *x, double *y) {
  coco_evaluate_function(PROBLEM, x, y);
}

/**
 * Calls coco_evaluate_function() to evaluate the single objective function
 * of the problem at the point x and returns y[0].
 */
double single_evaluate_function(const double *x, double *y) {
  coco_evaluate_function(PROBLEM, x, y);
  return y[0];
}

/**
 * Calls coco_evaluate_constraint() to evaluate the constraints
 * of the problem at the point x and stores the result in the vector y
 */
static void evaluate_constraint(const double *x, double *y) {
  coco_evaluate_constraint(PROBLEM, x, y);
}

/* Declarations of all functions implemented in this file (so that their order
 * is not important): */
void example_experiment(const char *suite_name, const char *suite_options,
                        const char *observer_name, const char *observer_options,
                        coco_random_state_t *random_generator);

void my_random_search(evaluate_function_t evaluate_func,
                      evaluate_function_t evaluate_cons, const size_t dimension,
                      const size_t number_of_objectives,
                      const size_t number_of_constraints,
                      const double *lower_bounds, const double *upper_bounds,
                      const size_t number_of_integer_variables,
                      const size_t max_budget,
                      coco_random_state_t *random_generator);

void my_grid_search(evaluate_function_t evaluate_func,
                    evaluate_function_t evaluate_cons, const size_t dimension,
                    const size_t number_of_objectives,
                    const size_t number_of_constraints,
                    const double *lower_bounds, const double *upper_bounds,
                    const size_t number_of_integer_variables,
                    const size_t max_budget);

void ecs(evaluate_function_t evaluate_func, evaluate_function_t evaluate_cons,
         size_t problem_dimension, const size_t number_of_objectives,
         size_t evaluations_remaining, const double *lower_bounds,
         const double *upper_bounds, int max_population,
         int mutation_likelihood, int n_clusters, double ppromis,
         int roulette, int seed);

/* Structure and functions needed for timing the experiment */
typedef struct {
  size_t number_of_dimensions;
  size_t current_idx;
  char **output;
  size_t previous_dimension;
  size_t cumulative_evaluations;
  time_t start_time;
  time_t overall_start_time;
} timing_data_t;
static timing_data_t *timing_data_initialize(coco_suite_t *suite);
static void timing_data_time_problem(timing_data_t *timing_data,
                                     coco_problem_t *problem);
static void timing_data_finalize(timing_data_t *timing_data);

/**
 * The main method initializes the random number generator and calls the example
 * experiment on the bbob suite.
 */
int main(void) {

  coco_random_state_t *random_generator = coco_random_new(RANDOM_SEED);

  /* Change the log level to "warning" to get less output */
  coco_set_log_level("info");

  printf("Running the example experiment... (might take time, be patient)\n");
  fflush(stdout);

  /**
   * Start the actual experiments on a test suite and use a matching logger, for
   * example one of the following:
   *   bbob                 24 unconstrained noiseless single-objective
   * functions bbob-biobj           55 unconstrained noiseless bi-objective
   * functions [bbob-biobj-ext       92 unconstrained noiseless bi-objective
   * functions] [bbob-constrained*   48 constrained noiseless single-objective
   * functions] bbob-largescale      24 unconstrained noiseless single-objective
   * functions in large dimension bbob-mixint          24 unconstrained
   * noiseless single-objective functions with mixed-integer variables
   *   bbob-biobj-mixint    92 unconstrained noiseless bi-objective functions
   * with mixed-integer variables
   *
   * Suites with a star are partly implemented but not yet fully supported.
   *
   * Adapt to your need. Note that the experiment is run according
   * to the settings, defined in example_experiment(...) below.
   */
  coco_set_log_level("info");

  /**
   * For more details on how to change the default suite and observer options,
   * see http://numbbo.github.io/coco-doc/C/#suite-parameters and
   * http://numbbo.github.io/coco-doc/C/#observer-parameters. */

  example_experiment("bbob", "dimensions: 2,3,5,10,20", "bbob",
                     "result_folder: ECS_on_bbob", random_generator);

  printf("Done!\n");
  fflush(stdout);

  coco_random_free(random_generator);

  return 0;
}

/**
 * A simple example of benchmarking random search on a given suite with default
 * instances that can serve also as a timing experiment.
 *
 * @param suite_name Name of the suite (e.g. "bbob" or "bbob-biobj").
 * @param suite_options Options of the suite (e.g. "dimensions: 2,3,5,10,20
 * instance_indices: 1-5").
 * @param observer_name Name of the observer matching with the chosen suite
 * (e.g. "bbob-biobj" when using the "bbob-biobj-ext" suite).
 * @param observer_options Options of the observer (e.g. "result_folder:
 * folder_name")
 * @param random_generator The random number generator.
 */
void example_experiment(const char *suite_name, const char *suite_options,
                        const char *observer_name, const char *observer_options,
                        coco_random_state_t *random_generator) {

  size_t run;
  coco_suite_t *suite;
  coco_observer_t *observer;
  timing_data_t *timing_data;

  /* Initialize the suite and observer. */
  suite = coco_suite(suite_name, "", suite_options);
  observer = coco_observer(observer_name, observer_options);

  /* Initialize timing */
  timing_data = timing_data_initialize(suite);

  /* ECS params */
  int max_pop = 30;
  int mutation_likelihood = 3;
  int n_clusters = 20;
  double ppromis = 2.2;
  int roulette = 0;
  int seed = 1;

  /* Iterate over all problems in the suite */
  while ((PROBLEM = coco_suite_get_next_problem(suite, observer)) != NULL) {

    size_t dimension = coco_problem_get_dimension(PROBLEM);

    /* Run the algorithm at least once */
    for (run = 1; run <= 1 + INDEPENDENT_RESTARTS; run++) {

      long evaluations_done =
          (long)(coco_problem_get_evaluations(PROBLEM) +
                 coco_problem_get_evaluations_constraints(PROBLEM));
      long evaluations_remaining =
          (long)(dimension * BUDGET_MULTIPLIER) - evaluations_done;

      /* Break the loop if the target was hit or there are no more remaining
       * evaluations */
      if ((coco_problem_final_target_hit(PROBLEM) &&
           coco_problem_get_number_of_constraints(PROBLEM) == 0) ||
          (evaluations_remaining <= 0))
        break;

      /* Call the optimization algorithm for the remaining number of evaluations
       */
      /*my_random_search(evaluate_function, evaluate_constraint, dimension,
                       coco_problem_get_number_of_objectives(PROBLEM),
                       coco_problem_get_number_of_constraints(PROBLEM),
                       coco_problem_get_smallest_values_of_interest(PROBLEM),
                       coco_problem_get_largest_values_of_interest(PROBLEM),
                       coco_problem_get_number_of_integer_variables(PROBLEM),
                       (size_t)evaluations_remaining, random_generator);*/

      ecs(evaluate_function, evaluate_constraint, dimension,
          coco_problem_get_number_of_objectives(PROBLEM),
          (size_t)evaluations_remaining,
          coco_problem_get_smallest_values_of_interest(PROBLEM),
          coco_problem_get_largest_values_of_interest(PROBLEM), max_pop,
          mutation_likelihood, n_clusters, ppromis, roulette, seed);

      /* Break the loop if the algorithm performed no evaluations or an
       * unexpected thing happened */
      if (coco_problem_get_evaluations(PROBLEM) == evaluations_done) {
        printf("WARNING: Budget has not been exhausted (%lu/%lu evaluations "
               "done)!\n",
               (unsigned long)evaluations_done,
               (unsigned long)dimension * BUDGET_MULTIPLIER);
        break;
      } else if (coco_problem_get_evaluations(PROBLEM) < evaluations_done)
        coco_error("Something unexpected happened - function evaluations were "
                   "decreased!");
    }

    /* Keep track of time */
    timing_data_time_problem(timing_data, PROBLEM);
  }

  /* Output and finalize the timing data */
  timing_data_finalize(timing_data);

  coco_observer_free(observer);
  coco_suite_free(suite);
}

void ecs(evaluate_function_t evaluate_func, evaluate_function_t evaluate_cons,
         size_t problem_dimension, const size_t number_of_objectives,
         size_t evaluations_remaining, const double *lower_bounds,
         const double *upper_bounds, int max_population,
         int mutation_likelihood, int n_clusters, double ppromis,
         int roulette, int seed) {
  Prototipos C;

  int numGeracoes, numCruza, bLocTot, bLocOk, nGrupos = 0;

  double erro, step, fit, dist;

  int achou, mutou;
  size_t i, j;

  int semente;

  int steps = 10 * problem_dimension;

  double *y = coco_allocate_vector(number_of_objectives);

  srand((unsigned)time(0) + semente);

  initPopulation(&P, max_population, problem_dimension);

  initClusters(&C, n_clusters, problem_dimension);

  C.limiar = (upper_bounds[0] - lower_bounds[0]) /
             (2.0F * pow(n_clusters, (1.0F / P.tamInd)));
  C.densid = (int)ppromis * NUMSELS * NUMCRU / n_clusters;

  generateIndividualsPopulation(&P, max_population, problem_dimension,
                                single_evaluate_function, y, lower_bounds[0],
                                upper_bounds[0]);

  achou = test_solution_found(PROBLEM);

  bLocTot = bLocOk = 0;
  numGeracoes = 1;

  while (i < evaluations_remaining && !achou) {

    numCruza = NUMCRU;

    while (!achou && numCruza && i < evaluations_remaining) {

      if (roulette) {
        rouletteSelectionPressure(&P, P.indiv[P.melhor].fit);
      } else {
        P.pai[0] = rand() % P.tamPop;
        P.pai[1] = (rand() % (P.pai[0] + 1)) + rand() % (P.tamPop - P.pai[0]);
      }

      if (updateGrp(&C, &P) >= C.densid) {
        step = PSGRI * C.limiar;
        EstratIIIntenso(&C, &P, &achou, &bLocOk, &bLocTot, step,
                        single_evaluate_function, PROBLEM, y, TAXERR / 10.0F,
                        steps, ESCALA, problem_dimension, lower_bounds[0],
                        upper_bounds[0]);
      }

      if (P.pai[0] != P.pai[1])
        CruzaBlend(&P, P.pai[0], P.pai[1], P.pior, BLXALFA, lower_bounds[0],
                   upper_bounds[0]);
      else
        P.iguais++;

      fit = single_evaluate_function(P.indiv[P.pior].var, y);

      if (fit >= P.indiv[P.melhor].fit) {
        mutou = MutaNaoUni(P.indiv[P.pior].var, P.tamInd, P.tamPop, numGeracoes,
                           MUTNUNI, mutation_likelihood, lower_bounds[0],
                           upper_bounds[0]);
        if (mutou) {
          P.numMuta++;
          fit = single_evaluate_function(P.indiv[P.pior].var, y);
        }
      }

      updatePopulation(&P, P.pior, fit, numGeracoes);

      achou = test_solution_found(PROBLEM);

      numCruza--;
    }

    if (!achou) {
      groupsCoolDown(&C);
      C.numGrp += !C.numGrp;
      C.limiar = (upper_bounds[0] - lower_bounds[0]) /
                 (2.0F * pow((C.numGrp), (1.0F / P.tamInd)));
      C.densid = (int)ppromis * NUMSELS * NUMCRU / (C.numGrp);
      nGrupos = C.numGrp;
    }

    i++;
  }
}

/**
 * A random search algorithm that can be used for single- as well as
 * multi-objective optimization. The problem's initial solution is evaluated
 * first.
 *
 * @param evaluate_func The function used to evaluate the objective function.
 * @param evaluate_cons The function used to evaluate the constraints.
 * @param dimension The number of variables.
 * @param number_of_objectives The number of objectives.
 * @param number_of_constraints The number of constraints.
 * @param lower_bounds The lower bounds of the region of interested (a vector
 * containing dimension values).
 * @param upper_bounds The upper bounds of the region of interested (a vector
 * containing dimension values).
 * @param number_of_integer_variables The number of integer variables (if > 0,
 * all integer variables come before any continuous ones).
 * @param max_budget The maximal number of evaluations.
 * @param random_generator Pointer to a random number generator able to produce
 * uniformly and normally distributed random numbers.
 */
void my_random_search(evaluate_function_t evaluate_func,
                      evaluate_function_t evaluate_cons, const size_t dimension,
                      const size_t number_of_objectives,
                      const size_t number_of_constraints,
                      const double *lower_bounds, const double *upper_bounds,
                      const size_t number_of_integer_variables,
                      const size_t max_budget,
                      coco_random_state_t *random_generator) {

  double *x = coco_allocate_vector(dimension);
  double *functions_values = coco_allocate_vector(number_of_objectives);
  double *constraints_values = NULL;
  double range;
  size_t i, j;

  if (number_of_constraints > 0)
    constraints_values = coco_allocate_vector(number_of_constraints);

  coco_problem_get_initial_solution(PROBLEM, x);
  evaluate_func(x, functions_values);

  for (i = 1; i < max_budget; ++i) {

    /* Construct x as a random point between the lower and upper bounds */
    for (j = 0; j < dimension; ++j) {
      range = upper_bounds[j] - lower_bounds[j];
      x[j] = lower_bounds[j] + coco_random_uniform(random_generator) * range;
      /* Round the variable if integer */
      if (j < number_of_integer_variables)
        x[j] = floor(x[j] + 0.5);
    }

    /* Evaluate COCO's constraints function if problem is constrained */
    if (number_of_constraints > 0)
      evaluate_cons(x, constraints_values);

    /* Call COCO's evaluate function where all the logging is performed */
    evaluate_func(x, functions_values);
  }

  coco_free_memory(x);
  coco_free_memory(functions_values);
  if (number_of_constraints > 0)
    coco_free_memory(constraints_values);
}

/**
 * A grid search optimizer that can be used for single- as well as
 * multi-objective optimization.
 *
 * @param evaluate_func The evaluation function used to evaluate the solutions.
 * @param evaluate_cons The function used to evaluate the constraints.
 * @param dimension The number of variables.
 * @param number_of_objectives The number of objectives.
 * @param number_of_constraints The number of constraints.
 * @param lower_bounds The lower bounds of the region of interested (a vector
 * containing dimension values).
 * @param upper_bounds The upper bounds of the region of interested (a vector
 * containing dimension values).
 * @param number_of_integer_variables The number of integer variables (if > 0,
 * all integer variables come before any continuous ones).
 * @param max_budget The maximal number of evaluations.
 *
 * If max_budget is not enough to cover even the smallest possible grid, only
 * the first max_budget nodes of the grid are evaluated.
 */
void my_grid_search(evaluate_function_t evaluate_func,
                    evaluate_function_t evaluate_cons, const size_t dimension,
                    const size_t number_of_objectives,
                    const size_t number_of_constraints,
                    const double *lower_bounds, const double *upper_bounds,
                    const size_t number_of_integer_variables,
                    const size_t max_budget) {

  double *x = coco_allocate_vector(dimension);
  double *func_values = coco_allocate_vector(number_of_objectives);
  double *cons_values = NULL;
  long *nodes = (long *)coco_allocate_memory(sizeof(long) * dimension);
  double *grid_step = coco_allocate_vector(dimension);
  size_t i, j;
  size_t evaluations = 0;
  long *max_nodes = (long *)coco_allocate_memory(sizeof(long) * dimension);
  long integer_nodes = 1;

  /* Initialization */
  for (j = 0; j < dimension; j++) {
    nodes[j] = 0;
    if (j < number_of_integer_variables) {
      grid_step[j] = 1;
      max_nodes[j] = (long)floor(upper_bounds[j] + 0.5);
      assert(fabs(lower_bounds[j]) < 1e-6);
      assert(max_nodes[j] > 0);
      integer_nodes *= max_nodes[j];
    } else {
      max_nodes[j] =
          (long)floor(
              pow((double)max_budget / (double)integer_nodes,
                  1 / (double)(dimension - number_of_integer_variables))) -
          1;
      /* Take care of the borderline case */
      if (max_nodes[j] < 1)
        max_nodes[j] = 1;
      grid_step[j] = (upper_bounds[j] - lower_bounds[j]) / (double)max_nodes[j];
    }
  }

  if (number_of_constraints > 0)
    cons_values = coco_allocate_vector(number_of_constraints);

  while (evaluations < max_budget) {

    /* Stop if there are no more nodes */
    if ((number_of_integer_variables == dimension) &&
        (evaluations >= integer_nodes))
      break;

    /* Construct x and evaluate it */
    for (j = 0; j < dimension; j++) {
      x[j] = lower_bounds[j] + grid_step[j] * (double)nodes[j];
    }

    /* Evaluate COCO's constraints function if problem is constrained */
    if (number_of_constraints > 0)
      evaluate_cons(x, cons_values);

    /* Call COCO's evaluate function where all the logging is performed */
    evaluate_func(x, func_values);
    evaluations++;

    /* Inside the grid, move to the next node */
    if (nodes[0] < max_nodes[0]) {
      nodes[0]++;
    }

    /* At an outside node of the grid, move to the next level */
    else if (max_nodes[0] > 0) {
      for (j = 1; j < dimension; j++) {
        if (nodes[j] < max_nodes[j]) {
          nodes[j]++;
          for (i = 0; i < j; i++)
            nodes[i] = 0;
          break;
        }
      }

      /* At the end of the grid, exit */
      if ((j == dimension) && (nodes[j - 1] == max_nodes[j - 1]))
        break;
    }
  }

  coco_free_memory(x);
  coco_free_memory(func_values);
  if (number_of_constraints > 0)
    coco_free_memory(cons_values);
  coco_free_memory(nodes);
  coco_free_memory(grid_step);
  coco_free_memory(max_nodes);
}

/**
 * Allocates memory for the timing_data_t object and initializes it.
 */
static timing_data_t *timing_data_initialize(coco_suite_t *suite) {

  timing_data_t *timing_data =
      (timing_data_t *)coco_allocate_memory(sizeof(*timing_data));
  size_t function_idx, dimension_idx, instance_idx, i;

  /* Find out the number of all dimensions */
  coco_suite_decode_problem_index(suite,
                                  coco_suite_get_number_of_problems(suite) - 1,
                                  &function_idx, &dimension_idx, &instance_idx);
  timing_data->number_of_dimensions = dimension_idx + 1;
  timing_data->current_idx = 0;
  timing_data->output = (char **)coco_allocate_memory(
      timing_data->number_of_dimensions * sizeof(char *));
  for (i = 0; i < timing_data->number_of_dimensions; i++) {
    timing_data->output[i] = NULL;
  }
  timing_data->previous_dimension = 0;
  timing_data->cumulative_evaluations = 0;
  time(&timing_data->start_time);
  time(&timing_data->overall_start_time);

  return timing_data;
}

/**
 * Keeps track of the total number of evaluations and elapsed time. Produces an
 * output string when the current problem is of a different dimension than the
 * previous one or when NULL.
 */
static void timing_data_time_problem(timing_data_t *timing_data,
                                     coco_problem_t *problem) {

  double elapsed_seconds = 0;

  if ((problem == NULL) || (timing_data->previous_dimension !=
                            coco_problem_get_dimension(problem))) {

    /* Output existing timing information */
    if (timing_data->cumulative_evaluations > 0) {
      time_t now;
      time(&now);
      elapsed_seconds = difftime(now, timing_data->start_time) /
                        (double)timing_data->cumulative_evaluations;
      timing_data->output[timing_data->current_idx++] =
          coco_strdupf("d=%lu done in %.2e seconds/evaluation\n",
                       timing_data->previous_dimension, elapsed_seconds);
    }

    if (problem != NULL) {
      /* Re-initialize the timing_data */
      timing_data->previous_dimension = coco_problem_get_dimension(problem);
      timing_data->cumulative_evaluations =
          coco_problem_get_evaluations(problem);
      time(&timing_data->start_time);
    }

  } else {
    timing_data->cumulative_evaluations +=
        coco_problem_get_evaluations(problem);
  }
}

/**
 * Outputs and finalizes the given timing data.
 */
static void timing_data_finalize(timing_data_t *timing_data) {

  /* Record the last problem */
  timing_data_time_problem(timing_data, NULL);

  if (timing_data) {
    size_t i;
    double elapsed_seconds;
    time_t now;
    int hours, minutes, seconds;

    time(&now);
    elapsed_seconds = difftime(now, timing_data->overall_start_time);

    printf("\n");
    for (i = 0; i < timing_data->number_of_dimensions; i++) {
      if (timing_data->output[i]) {
        printf("%s", timing_data->output[i]);
        coco_free_memory(timing_data->output[i]);
      }
    }
    hours = (int)elapsed_seconds / 3600;
    minutes = ((int)elapsed_seconds % 3600) / 60;
    seconds = (int)elapsed_seconds - (hours * 3600) - (minutes * 60);
    printf("Total elapsed time: %dh%02dm%02ds\n", hours, minutes, seconds);

    coco_free_memory(timing_data->output);
    coco_free_memory(timing_data);
  }
}
