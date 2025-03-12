#include <iostream>
#include <sstream>
#include <vector>
#include "z3++.h"
#include "tree_sitter/api.h"
#include "tree_sitter/tree-sitter-c-preproc.h"

using namespace z3;

#include "z3examples.hpp"

int main() {

    try {
        demorgan(); std::cout << "\n";
        find_model_example1(); std::cout << "\n";
        prove_example1(); std::cout << "\n";
        prove_example2(); std::cout << "\n";
        nonlinear_example1(); std::cout << "\n";
        bitvector_example1(); std::cout << "\n";
        bitvector_example2(); std::cout << "\n";
        capi_example(); std::cout << "\n";
        eval_example1(); std::cout << "\n";
        two_contexts_example1(); std::cout << "\n";
        error_example(); std::cout << "\n";
        numeral_example(); std::cout << "\n";
        ite_example(); std::cout << "\n";
        ite_example2(); std::cout << "\n";
        quantifier_example(); std::cout << "\n";
        unsat_core_example1(); std::cout << "\n";
        unsat_core_example2(); std::cout << "\n";
        unsat_core_example3(); std::cout << "\n";
        tactic_example1(); std::cout << "\n";
        tactic_example2(); std::cout << "\n";
        tactic_example3(); std::cout << "\n";
        tactic_example4(); std::cout << "\n";
        tactic_example5(); std::cout << "\n";
        tactic_example6(); std::cout << "\n";
        tactic_example7(); std::cout << "\n";
        tactic_example8(); std::cout << "\n";
        tactic_example9(); std::cout << "\n";
        tactic_qe(); std::cout << "\n";
        tst_visit(); std::cout << "\n";
        tst_numeral(); std::cout << "\n";
        incremental_example1(); std::cout << "\n";
        incremental_example2(); std::cout << "\n";
        incremental_example3(); std::cout << "\n";
        enum_sort_example(); std::cout << "\n";
        tuple_example(); std::cout << "\n";
        datatype_example(); std::cout << "\n";
        expr_vector_example(); std::cout << "\n";
        exists_expr_vector_example(); std::cout << "\n";
        substitute_example(); std::cout << "\n";
        opt_example(); std::cout << "\n";
        opt_translate_example(); std::cout << "\n";
        extract_example(); std::cout << "\n";
        param_descrs_example(); std::cout << "\n";
        sudoku_example(); std::cout << "\n";
        consequence_example(); std::cout << "\n";
        parse_example(); std::cout << "\n";
        parse_string(); std::cout << "\n";
        mk_model_example(); std::cout << "\n";
        recfun_example(); std::cout << "\n";
        string_values(); std::cout << "\n";
        string_issue_2298(); std::cout << "\n";
	iterate_args(); std::cout << "\n";
        std::cout << "done\n";
    }
    catch (exception & ex) {
        std::cout << "unexpected error: " << ex << "\n";
    }
    Z3_finalize_memory();
    return 0;
}
