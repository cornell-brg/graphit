//
// Created by Yunming Zhang on 2/14/17.
//

#include <graphit/backend/codegen_cpp.h>
#include <graphit/midend/mir.h>

namespace graphit {
    int CodeGenCPP::genCPP() {
        genIncludeStmts();
        genEdgeSets();
        //genElementData();
        genStructTypeDecls();
        genTypesRequiringTypeDefs();

        //Processing the constants, generting declartions
        for (auto constant : mir_context_->getLoweredConstants()) {
            if ((std::dynamic_pointer_cast<mir::VectorType>(constant->type)) != nullptr) {
                mir::VectorType::Ptr type = std::dynamic_pointer_cast<mir::VectorType>(constant->type);
                // if the constant decl is a field property of an element (system vector)
                //if (type->element_type != nullptr) {
                    //genPropertyArrayImplementationWithInitialization(constant);
                    //NOTE: here we only generate the declaration, not the allocation and initialization
                    // even through we have all the information.
                    // This is because we want to do the allocation and initialization steps in the main function,
                    // when we are using command line arguments and variables. This also allows flexibility of array of structs
                    // and struct of arrays.
                    // To support this feature, we have specialized the code generation of main function (see func_decl visit method).
                    // We first generate allocation, and then initialization (init_stmts) for global variables.
                    genPropertyArrayDecl(constant);
                //}
            } else if (std::dynamic_pointer_cast<mir::VertexSetType>(constant->type)) {
                // if the constant is a vertex set  decl
                // currently, no code is generated
            } else {
                // regular constant declaration
                //constant->accept(this);
                genScalarDecl(constant);
            }
        }

        // Generate global declarations for socket-local buffers used by NUMA optimization
        for (auto iter : mir_context_->edgeset_to_label_to_merge_reduce) {
            for (auto inner_iter : iter.second) {
                if (inner_iter.second->numa_aware) {
                    inner_iter.second->scalar_type->accept(this);
                    oss << " **local_" << inner_iter.second->field_name << ";" << std::endl;
                }
            }
        }

        //Generates function declarations for various edgeset apply operations with different schedules
        // TODO: actually complete the generation, fow now we will use libraries to test a few schedules
        auto gen_edge_apply_function_visitor = EdgesetApplyFunctionDeclGenerator(mir_context_, oss);
        gen_edge_apply_function_visitor.genEdgeApplyFuncDecls();

        //Processing the functions
        std::map<std::string, mir::FuncDecl::Ptr>::iterator it;
        std::vector<mir::FuncDecl::Ptr> functions = mir_context_->getFunctionList();
        std::vector<mir::FuncDecl::Ptr> extern_functions = mir_context_->getExternFunctionList();

        for (auto it = extern_functions.begin(); it != extern_functions.end(); it++) {
            it->get()->accept(this);
        }

        for (auto it = functions.begin(); it != functions.end(); it++) {
	        it->get()->accept(this);
        }

	generatePyBindModule();
        oss << std::endl;
        return 0;
    };
    void CodeGenCPP::generatePyBindModule() {
	oss << "#ifdef GEN_PYBIND_WRAPPERS" << std::endl;
	oss << "PYBIND11_MODULE(" << module_name << ", m) {" << std::endl;
	indent();

        std::vector<mir::FuncDecl::Ptr> functions = mir_context_->getFunctionList();
	for (auto it = functions.begin(); it != functions.end(); it++) {
		mir::FuncDecl::Ptr func_decl = *it;
		if (func_decl->type == mir::FuncDecl::Type::EXPORTED) {
			oss << "m.def(\"" << func_decl->name << "\", &" << func_decl->name << "__wrapper, \"\");" << std::endl;
		}
	}
	dedent();
	oss << "}" << std::endl;		

	oss << "#endif" << std::endl;
    }
    void CodeGenCPP::genIncludeStmts() {
        oss << "#include <iostream> " << std::endl;
        oss << "#include <vector>" << std::endl;
	oss << "#include <algorithm>" << std::endl;
        oss << "#include \"intrinsics.h\"" << std::endl;
	
	oss << "#ifdef GEN_PYBIND_WRAPPERS" << std::endl;
	oss << "#include <pybind11/pybind11.h>" << std::endl;
	oss << "#include <pybind11/stl.h>" << std::endl;
	oss << "#include <pybind11/numpy.h>" << std::endl;
	oss << "namespace py = pybind11;" << std::endl;
	oss << "#endif" << std::endl;
	
	
    }

    void CodeGenCPP::visit(mir::ForStmt::Ptr for_stmt) {
        printIndent();
        auto for_domain = for_stmt->domain;
        auto loop_var = for_stmt->loopVar;
        oss << "for ( int " << loop_var << " = ";
        for_domain->lower->accept(this);
        oss << "; " << loop_var << " < ";
        for_domain->upper->accept(this);
        oss << "; " << loop_var << "++ )" << std::endl;
        printBeginIndent();
        indent();
        for_stmt->body->accept(this);
        dedent();
        printEndIndent();
        oss << std::endl;

    }

    void CodeGenCPP::visit(mir::WhileStmt::Ptr while_stmt) {
        printIndent();
        oss << "while ( ";
        while_stmt->cond->accept(this);
        oss << ")" << std::endl;
        printBeginIndent();
        indent();
        while_stmt->body->accept(this);
        dedent();
        printEndIndent();
        oss << std::endl;

    }

    void CodeGenCPP::visit(mir::IfStmt::Ptr stmt) {
        printIndent();
        oss << "if (";
        stmt->cond->accept(this);
        oss << ")" << std::endl;

        printIndent();
        oss << " { " << std::endl;

        indent();
        stmt->ifBody->accept(this);
        dedent();

        printIndent();
        oss << " } " << std::endl;

        if (stmt->elseBody) {
            printIndent();
            oss << "else" << std::endl;

            printIndent();
            oss << " { " << std::endl;

            indent();
            stmt->elseBody->accept(this);
            dedent();

            oss << std::endl;

            printIndent();
            oss << " } " << std::endl;

        }

        //printIndent();
        //oss << "end";

    }

    void CodeGenCPP::visit(mir::ExprStmt::Ptr expr_stmt) {

        if (mir::isa<mir::EdgeSetApplyExpr>(expr_stmt->expr)) {
            printIndent();
            auto edgeset_apply_expr = mir::to<mir::EdgeSetApplyExpr>(expr_stmt->expr);
            genEdgesetApplyFunctionCall(edgeset_apply_expr);
        } else {
            printIndent();
            expr_stmt->expr->accept(this);
            oss << ";" << std::endl;
        }
    }

    void CodeGenCPP::visit(mir::AssignStmt::Ptr assign_stmt) {
        // Removing this special case because the filter is now handled by intrinsics
/*
        if (mir::isa<mir::VertexSetWhereExpr>(assign_stmt->expr)) {
            // declaring a new vertexset as output from where expression
            printIndent();
            assign_stmt->expr->accept(this);
            oss << std::endl;

            printIndent();

            assign_stmt->lhs->accept(this);
            oss << "  = ____graphit_tmp_out; "  << std::endl;

        } else 
*/
        if (mir::isa<mir::EdgeSetApplyExpr>(assign_stmt->expr)) {
            printIndent();
            assign_stmt->lhs->accept(this);
            oss << " = ";
            auto edgeset_apply_expr = mir::to<mir::EdgeSetApplyExpr>(assign_stmt->expr);
            genEdgesetApplyFunctionCall(edgeset_apply_expr);

        } else {
            printIndent();
            assign_stmt->lhs->accept(this);
            oss << " = ";
            assign_stmt->expr->accept(this);
            oss << ";" << std::endl;
        }
    }

    void CodeGenCPP::visit(mir::CompareAndSwapStmt::Ptr cas_stmt) {
        printIndent();
        oss << cas_stmt->tracking_var_ << " = compare_and_swap ( ";
        cas_stmt->lhs->accept(this);
        oss << ", ";
        cas_stmt->compare_val_expr->accept(this);
        oss << ", ";
        cas_stmt->expr->accept(this);
        oss << ");" << std::endl;
    }

    void CodeGenCPP::visit(mir::ReduceStmt::Ptr reduce_stmt) {

        if (mir::isa<mir::VertexSetWhereExpr>(reduce_stmt->expr) ||
            mir::isa<mir::EdgeSetApplyExpr>(reduce_stmt->expr)) {


        } else {
            switch (reduce_stmt->reduce_op_) {
                case mir::ReduceStmt::ReductionOp::SUM:
                    printIndent();
                    reduce_stmt->lhs->accept(this);
                    oss << " += ";
                    reduce_stmt->expr->accept(this);
                    oss << ";" << std::endl;

                    if (reduce_stmt->tracking_var_name_ != "") {
                        // need to set the tracking variable
                        printIndent();
                        oss << reduce_stmt->tracking_var_name_ << " = true ; " << std::endl;
                    }

                    break;
                case mir::ReduceStmt::ReductionOp::MIN:
                    printIndent();
                    oss << "if ( ( ";
                    reduce_stmt->lhs->accept(this);
                    oss << ") > ( ";
                    reduce_stmt->expr->accept(this);
                    oss << ") ) { " << std::endl;
                    indent();
                    printIndent();
                    reduce_stmt->lhs->accept(this);
                    oss << "= ";
                    reduce_stmt->expr->accept(this);
                    oss << "; " << std::endl;


                    if (reduce_stmt->tracking_var_name_ != "") {
                        // need to generate a tracking variable
                        printIndent();
                        oss << reduce_stmt->tracking_var_name_ << " = true ; " << std::endl;
                    }

                    dedent();
                    printIndent();
                    oss << "} " << std::endl;
                    break;
                case mir::ReduceStmt::ReductionOp::MAX:
                    //TODO: not supported yet

                    oss << " max= ";
                    break;
                case mir::ReduceStmt::ReductionOp::ATOMIC_MIN:
                    printIndent();
                    oss << reduce_stmt->tracking_var_name_ << " = ";
                    oss << "writeMin( &";
                    reduce_stmt->lhs->accept(this);
                    oss << ", ";
                    reduce_stmt->expr->accept(this);
                    oss << " ); " << std::endl;
                    break;
                case mir::ReduceStmt::ReductionOp::ATOMIC_SUM:
                    printIndent();
                    if (reduce_stmt->tracking_var_name_ != "")
                        oss << reduce_stmt->tracking_var_name_ << " =  true;\n";
                    oss << "writeAdd( &";
                    reduce_stmt->lhs->accept(this);
                    oss << ", ";
                    reduce_stmt->expr->accept(this);
                    oss << " ); " << std::endl;
                    break;
            }

        }
    }

    void CodeGenCPP::visit(mir::PrintStmt::Ptr print_stmt) {
        printIndent();
        oss << "std::cout << ";
        print_stmt->expr->accept(this);
        oss << "<< std::endl;" << std::endl;
    }

    void CodeGenCPP::visit(mir::BreakStmt::Ptr print_stmt) {
        printIndent();
        oss << "break;" << std::endl;
    }

    void CodeGenCPP::visit(mir::VarDecl::Ptr var_decl) {
        // Removing this special case because we want to generate intrinsics for filter
/*
        if (mir::isa<mir::VertexSetWhereExpr>(var_decl->initVal)) {
            // declaring a new vertexset as output from where expression
            printIndent();
            var_decl->initVal->accept(this);
            oss << std::endl;

            printIndent();
            var_decl->type->accept(this);
            oss << var_decl->name << "  = ____graphit_tmp_out; " << std::endl;

        } else 
*/
        if (mir::isa<mir::EdgeSetApplyExpr>(var_decl->initVal)) {
            printIndent();
            var_decl->type->accept(this);
            oss << var_decl->name << " = ";
            auto edgeset_apply_expr = mir::to<mir::EdgeSetApplyExpr>(var_decl->initVal);
            genEdgesetApplyFunctionCall(edgeset_apply_expr);
        } else {
            printIndent();

            //we probably don't need the modifiers now
            //oss << var_decl->modifier << ' ';

            var_decl->type->accept(this);
            oss << var_decl->name << " ";
            if (var_decl->initVal != nullptr) {
                oss << "= ";
                var_decl->initVal->accept(this);
            }
            oss << ";" << std::endl;
        }
    }


    void CodeGenCPP::visit(mir::FuncDecl::Ptr func_decl) {

	if (func_decl->type == mir::FuncDecl::Type::EXTERNAL) {
	    oss << "extern ";
            if (func_decl->result.isInitialized())
	        func_decl->result.getType()->accept(this);
            else
                oss << "void ";
            oss << func_decl->name << " (";
            
            bool printDelimiter = false;
            for (auto arg : func_decl->args) {
                if (printDelimiter) {
                    oss << ", ";
                }

                arg.getType()->accept(this);
                oss << arg.getName();
                printDelimiter = true;
            }
            if (!printDelimiter)
                oss << "void";
            oss << "); ";
            oss << std::endl;
            return;
	}

        // Generate function signature
        if (func_decl->name == "main") {
            func_decl->isFunctor = false;
            oss << "int " << func_decl->name << "(int argc, char * argv[])";
        } else {
            // Use functors for better compiler inlining
            func_decl->isFunctor = true;
            oss << "struct " << func_decl->name << std::endl;
            printBeginIndent();
            indent();
            oss << std::string(2 * indentLevel, ' ');

            if (func_decl->result.isInitialized()) {
                func_decl->result.getType()->accept(this);

                //insert an additional var_decl for returning result
                const auto var_decl = std::make_shared<mir::VarDecl>();
                var_decl->name = func_decl->result.getName();
                var_decl->type = func_decl->result.getType();
                if (func_decl->body->stmts == nullptr) {
                    func_decl->body->stmts = new std::vector<mir::Stmt::Ptr>();
                }
                auto it = func_decl->body->stmts->begin();
                func_decl->body->stmts->insert(it, var_decl);
            } else {
                oss << "void ";
            }

            oss << "operator() (";
            bool printDelimiter = false;
            for (auto arg : func_decl->args) {
                if (printDelimiter) {
                    oss << ", ";
                }

                arg.getType()->accept(this);
                oss << arg.getName();
                printDelimiter = true;
            }
            oss << ") ";
        }

        oss << std::endl;
        printBeginIndent();
        indent();

        if (func_decl->name == "main") {
            //generate special initialization code for main function
            //TODO: this is probably a hack that could be fixed for later

            //First, allocate the edgesets (read them from outside files if needed)
            for (auto stmt : mir_context_->edgeset_alloc_stmts) {
                stmt->accept(this);
            }

            // Initialize graphSegments if necessary
            auto segment_map = mir_context_->edgeset_to_label_to_num_segment;
            for (auto edge_iter = segment_map.begin(); edge_iter != segment_map.end(); edge_iter++) {
                auto edgeset = mir_context_->getConstEdgeSetByName((*edge_iter).first);
                auto edge_set_type = mir::to<mir::EdgeSetType>(edgeset->type);
                bool is_weighted = (edge_set_type->weight_type != nullptr);
                for (auto label_iter = (*edge_iter).second.begin();
                     label_iter != (*edge_iter).second.end(); label_iter++) {
                    auto edge_iter_first = (*edge_iter).first;
                    auto label_iter_first = (*label_iter).first;
                    auto label_iter_second = (*label_iter).second;
                    auto numa_aware_flag = mir_context_->edgeset_to_label_to_merge_reduce[edge_iter_first][label_iter_first]->numa_aware;

                    if (label_iter_second < 0) {
                        //do a specical case for negative number of segments. I
                        // in the case of negative integer, we use the number as argument to runtimve argument argv
                        // this is the only place in the generated code that we set the number of segments
                        oss << "  " << edgeset->name << ".buildPullSegmentedGraphs(\"" << label_iter_first
                            << "\", " << "atoi(argv[" << -1*label_iter_second << "])"
                            << (numa_aware_flag ? ", true" : "") << ");" << std::endl;
                    } else {
                        // just use the positive integer as argument to number of segments
                        oss << "  " << edgeset->name << ".buildPullSegmentedGraphs(\"" << label_iter_first
                            << "\", " << label_iter_second
                            << (numa_aware_flag ? ", true" : "") << ");" << std::endl;
                    }
                }
            }

            //generate allocation statemetns for field vectors
            for (auto constant : mir_context_->getLoweredConstants()) {
                if ((std::dynamic_pointer_cast<mir::VectorType>(constant->type)) != nullptr) {
                    mir::VectorType::Ptr type = std::dynamic_pointer_cast<mir::VectorType>(constant->type);
                    // if the constant decl is a field property of an element (system vector)
                    if (type->element_type != nullptr) {
                        //genPropertyArrayImplementationWithInitialization(constant);
                        //genPropertyArrayDecl(constant);
			if (constant->needs_allocation)
	                        genPropertyArrayAlloc(constant);
                    }
                } else if (std::dynamic_pointer_cast<mir::VertexSetType>(constant->type)) {
                    // if the constant is a vertex set  decl
                    // currently, no code is generated
                } else {
                    // regular constant declaration
                    //constant->accept(this);
                    genScalarAlloc(constant);
                }
            }

            // the stmts that initializes the field vectors
            for (auto stmt : mir_context_->field_vector_init_stmts) {
                stmt->accept(this);
            }

            for (auto iter : mir_context_->edgeset_to_label_to_merge_reduce) {
                for (auto inner_iter : iter.second) {

                    if ((inner_iter.second)->numa_aware) {
                        auto merge_reduce = inner_iter.second;
                        std::string local_field = "local_" + merge_reduce->field_name;
                        oss << "  " << local_field << " = new ";
                        merge_reduce->scalar_type->accept(this);
                        oss << "*[omp_get_num_places()];\n";

                        oss << "  for (int socketId = 0; socketId < omp_get_num_places(); socketId++) {\n";
                        oss << "    " << local_field << "[socketId] = (";
                        merge_reduce->scalar_type->accept(this);
                        oss << "*)numa_alloc_onnode(sizeof(";
                        merge_reduce->scalar_type->accept(this);
                        oss << ") * ";
                        auto count_expr = mir_context_->getElementCount(
                                mir_context_->getElementTypeFromVectorOrSetName(merge_reduce->field_name));
                        count_expr->accept(this);
                        oss << ", socketId);\n";

                        oss << "    parallel_for (int n = 0; n < ";
                        count_expr->accept(this);
                        oss << "; n++) {\n";
                        oss << "      " << local_field << "[socketId][n] = " << merge_reduce->field_name << "[n];\n";
                        oss << "    }\n  }\n";

                        oss << "  omp_set_nested(1);" << std::endl;
                    }
                }
            }
        } //end of if "main" condition

        // still generate the constant declarations
        if (func_decl->type == mir::FuncDecl::Type::EXPORTED){
            for (auto constant : mir_context_->getLoweredConstants()) {
                if (mir::isa<mir::ScalarType>(constant->type) &&
                        constant->initVal != nullptr){
                    genScalarAlloc(constant);
                }
            }
        }


        //if the function has a body
        if (func_decl->body && func_decl->body->stmts) {


            func_decl->body->accept(this);

            //print a return statemetn if there is a result
            if (func_decl->result.isInitialized()) {
                printIndent();
                oss << "return " << func_decl->result.getName() << ";" << std::endl;
            }


        }

        if (func_decl->isFunctor) {
          dedent();
          printEndIndent();
          oss << ";";
          oss << std::endl;
        }

        if (func_decl->name == "main") {
            for (auto iter : mir_context_->edgeset_to_label_to_merge_reduce) {
                for (auto inner_iter : iter.second) {
                    if (inner_iter.second->numa_aware) {
                        auto merge_reduce = inner_iter.second;
                        oss << "  for (int socketId = 0; socketId < omp_get_num_places(); socketId++) {\n";
                        oss << "    numa_free(local_" << merge_reduce->field_name << "[socketId], sizeof(";
                        merge_reduce->scalar_type->accept(this);
                        oss << ") * ";
                        mir_context_->getElementCount(mir_context_->getElementTypeFromVectorOrSetName(merge_reduce->field_name))->accept(this);
                        oss << ");\n  }\n";
                    }
                }
            }
        }

        dedent();
        printEndIndent();
        oss << ";";
        oss << std::endl;
	if (func_decl-> type == mir::FuncDecl::Type::EXPORTED) {
		generatePyBindWrapper(func_decl);
	}
    };
    void CodeGenCPP::generatePyBindWrapper(mir::FuncDecl::Ptr func_decl) {
	    oss << "#ifdef GEN_PYBIND_WRAPPERS" << std::endl;
	    oss << "//PyBind Wrappers for function" << func_decl->name << std::endl;
	    //Currently we do no support, returning Graph Types. So return type can be directly emitted without extra checks	
	    if (func_decl->result.isInitialized())
		    if (mir::isa<mir::VectorType>(func_decl->result.getType())) {

		        mir::VectorType::Ptr vector_type = mir::to<mir::VectorType>(func_decl->result.getType());
			oss << "py::array_t<";

			if (mir::isa<mir::VectorType>(vector_type->vector_element_type)) 
				mir::to<mir::VectorType>(vector_type->vector_element_type)->vector_element_type->accept(this);
			else 
				vector_type->vector_element_type->accept(this);
			oss << "> ";
		    }
		    else 
		        func_decl->result.getType()->accept(this);
	    else
		    oss << "void ";
	    oss << func_decl->name << "__wrapper (";
	    // For argument types we need to check if it is a graph, if it is, we need to expand it into 3 numpy arrays
	    bool printDelimiter = false;
	    for (auto arg : func_decl->args) {
		    if (printDelimiter) {
			    oss << ", ";
		    }
		    if (mir::isa<mir::EdgeSetType>(arg.getType())) {
			    oss << "py::object _" << arg.getName();
		    }else if (mir::isa<mir::VectorType>(arg.getType())) {
			    // We want to support vectors of vectors of scalar types separately
		            mir::VectorType::Ptr vector_type = mir::to<mir::VectorType>(arg.getType());
			    mir::Type::Ptr elem_type = vector_type->vector_element_type;
			    if (mir::isa<mir::VectorType>(elem_type)) {
				    mir::VectorType::Ptr inner_vector_type = mir::to<mir::VectorType>(elem_type);
				    oss << "py::array_t<";
                                    inner_vector_type->vector_element_type->accept(this);
                                    oss << ">";
                                    oss << " _" << arg.getName(); 
			    }else { 
				    oss << "py::array_t<";
				    vector_type->vector_element_type->accept(this);
				    oss << ">";	
				    oss << " _" << arg.getName();
			    }
		    }else {
			    arg.getType()->accept(this);
			    oss << arg.getName();
		    }
		    printDelimiter = true;
	    }
	    if (!printDelimiter)
		    oss << "void";
	    oss << ") ";	
	    oss << "{ " << std::endl;
	    indent();
	    // Need to generate translation for graph arguments before the actual call

	    for (auto arg : func_decl->args) {
		    if (mir::isa<mir::EdgeSetType>(arg.getType())) {
			    mir::EdgeSetType::Ptr type = mir::to<mir::EdgeSetType>(arg.getType());
			    if (type->weight_type != NULL) {
				   printIndent();
				   oss << "py::array_t<";
				   type->weight_type->accept(this);
				   oss << "> " << arg.getName() << "__data = _" << arg.getName() << ".attr(\"data\").cast<py::array_t<"; 
				   type->weight_type->accept(this);
				   oss << ">>();" << std::endl;

				    printIndent();
				    oss << "py::array_t<int> " << arg.getName() << "__indices = _" << arg.getName() << ".attr(\"indices\").cast<py::array_t<int>>();" << std::endl;
				    printIndent();
				    oss << "py::array_t<int> " << arg.getName() << "__indptr = _" << arg.getName() << ".attr(\"indptr\").cast<py::array_t<int>>();" << std::endl;
				    printIndent();
				    arg.getType()->accept(this);
				    oss << arg.getName() << " = builtin_loadWeightedEdgesFromCSR(";
				    oss << arg.getName() << "__data.data(), " << arg.getName() << "__indptr.data(), " << arg.getName() << "__indices.data(), " << arg.getName() << "__indptr.size()-1, " << arg.getName() << "__indices.size());" << std::endl; 
			    } else {	
				    //Prepare the individual arrays from the object
				    printIndent();
				    oss << "py::array_t<int> " << arg.getName() << "__data = _" << arg.getName() << ".attr(\"data\").cast<py::array_t<int>>();" << std::endl;
				    printIndent();
				    oss << "py::array_t<int> " << arg.getName() << "__indices = _" << arg.getName() << ".attr(\"indices\").cast<py::array_t<int>>();" << std::endl;
				    printIndent();
				    oss << "py::array_t<int> " << arg.getName() << "__indptr = _" << arg.getName() << ".attr(\"indptr\").cast<py::array_t<int>>();" << std::endl;
				    printIndent();
				    arg.getType()->accept(this);
				    oss << arg.getName() << " = builtin_loadEdgesFromCSR(";
				    oss << arg.getName() << "__indptr.data(), " << arg.getName() << "__indices.data(), " << arg.getName() << "__indptr.size()-1, " << arg.getName() << "__indices.size());" << std::endl; 
			    }
			
		    } else if (mir::isa<mir::VectorType>(arg.getType())) {
			    mir::VectorType::Ptr vector_type = mir::to<mir::VectorType>(arg.getType());
			    printIndent();
			    vector_type->accept(this);
			    oss << " " << arg.getName() << " = (";
			    vector_type->accept(this);
			    oss << ")_" << arg.getName() << ".data();" << std::endl; 
		    }

	    }
	    printIndent();
		
	    if (func_decl->result.isInitialized()) {
		    
		    func_decl->result.getType()->accept(this);
		    oss << "__" << func_decl->result.getName() << " = ";
	    }
	    oss << func_decl->name;
	    if (func_decl->isFunctor)
		    oss << "()";
	    oss << "(";
	    printDelimiter = false;
	    for (auto arg : func_decl->args) {
		    if (printDelimiter) {
			    oss << ", ";
		    }
		    oss << arg.getName();
		    printDelimiter = true;
	    }
	    oss << ");" << std::endl;
	    // We do no support returning Graph types. But we can return still return vectors	    
	    if (func_decl->result.isInitialized() ) { 
		    if (mir::isa<mir::VectorType>(func_decl->result.getType())) {
			    mir::VectorType::Ptr vector_type = mir::to<mir::VectorType>(func_decl->result.getType());
			    // Handle separately if vector of vector
			    if (mir::isa<mir::VectorType>(vector_type->vector_element_type)) {
				    mir::VectorType::Ptr inner_vector_type = mir::to<mir::VectorType> (vector_type->vector_element_type);
				    printIndent();
				    oss << "py::array_t<";
				    inner_vector_type->vector_element_type->accept(this);
				    oss << "> " << func_decl->result.getName() << " = py::array_t<";
				    inner_vector_type->vector_element_type->accept(this);
				    oss << "> ( std::vector<size_t>{(size_t)";
				    if (vector_type->element_type != nullptr)
				        mir_context_->getElementCount(vector_type->element_type)->accept(this);
				    else
					oss << vector_type->range_indexset;
				    oss << ", (size_t)";
				    oss << inner_vector_type->range_indexset;
				    oss << "}, std::vector<size_t>{ ";
				    oss << "( " << inner_vector_type->range_indexset << " * " << "sizeof(";
				    inner_vector_type->vector_element_type->accept(this);
				    oss << ")), sizeof(";
				    inner_vector_type->vector_element_type->accept(this);
				    oss << ") }, (";
				    inner_vector_type->vector_element_type->accept(this);
				    oss << "*)__" << func_decl->result.getName() << ");" << std::endl;
				
				    
			    } else   {
				    // Create the return object
				    printIndent();
				    oss << "py::array_t<";
				    vector_type->vector_element_type->accept(this);
				    oss << "> " << func_decl->result.getName() << " = py::array_t<";
				    vector_type->vector_element_type->accept(this);
				    oss << "> ( {";
				    if (vector_type->element_type != nullptr){
				        // get the size information of the output by looking up the count of the associated Element (e.g. Vertex) type
                        mir_context_->getElementCount(vector_type->element_type)->accept(this);
				    } else if (vector_type->range_indexset > 0) {
				        // the vector has range index associated with it
				        oss << vector_type->range_indexset;
				    }
				    oss << "}, { sizeof(";
				    vector_type->vector_element_type->accept(this);
				    oss << ") }, __" << func_decl->result.getName() << ");" << std::endl; 
			    }
			    
		    } else {
		            printIndent();
			    func_decl->result.getType()->accept(this);
			    oss << func_decl->result.getName() << " = __";
			    oss << func_decl->result.getName() << ";" << std::endl;
		    }
	    }
	    if (func_decl->result.isInitialized()) {
		    printIndent();
		    oss << "return " << func_decl->result.getName() << ";" << std::endl;
	    }		
	    dedent();
	    printIndent();
	    oss << "}" << std::endl;
	    oss << "#endif" << std::endl;

    }

    void CodeGenCPP::visit(mir::ScalarType::Ptr scalar_type) {
        switch (scalar_type->type) {
            case mir::ScalarType::Type::INT:
                oss << "int ";
                break;
            case mir::ScalarType::Type::FLOAT:
                oss << "float ";
                break;
            case mir::ScalarType::Type::DOUBLE:
                oss << "double ";
                break;
            case mir::ScalarType::Type::BOOL:
                oss << "bool ";
                break;
            case mir::ScalarType::Type::STRING:
                oss << "string ";
                break;
            default:
                break;
        }
    }

    void CodeGenCPP::visit(mir::VectorType::Ptr vector_type) {
        if (mir::isa<mir::ScalarType>(vector_type->vector_element_type)){
            vector_type->vector_element_type->accept(this);
        } else if (mir::isa<mir::VectorType>(vector_type->vector_element_type)){
            //nested vector type
            mir::VectorType::Ptr inner_vector_type = mir::to<mir::VectorType>(vector_type->vector_element_type);
            // use the typedef type for the inner vector type
            oss << inner_vector_type->toString();
        }
        oss << " * ";
    }

    void CodeGenCPP::visit(mir::StructTypeDecl::Ptr struct_type) {
        oss << struct_type->name << " ";
    }

    void CodeGenCPP::visit(mir::Call::Ptr call_expr) {
        oss << call_expr->name;


        if (call_expr->generic_type != nullptr) {
            oss << " < ";
            call_expr->generic_type->accept(this);
            oss << " > ";
        }

	    if (mir_context_->isFunction(call_expr->name)) {
            auto mir_func_decl = mir_context_->getFunction(call_expr->name);
            if (mir_func_decl->isFunctor)
                oss << "()";
        }

        oss << "(";

        bool printDelimiter = false;

        for (auto arg : call_expr->args) {
            if (printDelimiter) {
                oss << ", ";
            }
            arg->accept(this);
            printDelimiter = true;
        }

        oss << ") ";
    };

    /**
     * DEPRECATED, we don't generate code for TensorReadExpr
     * We only generate code for TensorArrayReadExpr and TensorStructReadExpr
     * @param expr
     */
//    void CodeGenCPP::visit(mir::TensorReadExpr::Ptr expr) {
//        //for dense array tensor read
//        expr->target->accept(this);
//        oss << "[";
//        expr->index->accept(this);
//        oss << "]";
//    };

/**
 * Generate tensor read code for array implementation
 * @param expr
 */
    void CodeGenCPP::visit(mir::TensorArrayReadExpr::Ptr expr) {
        //for dense array tensor read
//        expr->target->accept(this);
//        oss << "[";
//        expr->index->accept(this);
//        oss << "]";
//    }
        if (mir::isa<mir::MIRNode>(expr.get()->target.get()->shared_from_this())) {
            //not sure what this is std::shared_ptr<mir::MIRNode> ptr = expr.get()->target.get()->shared_from_this();
            std::string nameptr = expr.get()->getTargetNameStr();
            if (nameptr == "argv"){
                expr->target->accept(this);
                oss << "_safe(";
                expr->index->accept(this);
                oss << ", argv, argc)";
            }
            else{
                expr->target->accept(this);
                oss << "[";
                expr->index->accept(this);
                oss << "]";
            }

        } else {
            expr->target->accept(this);
            oss << "[";
            expr->index->accept(this);
            oss << "]";
        }
    }

/**
 * Generate tensor read code for struct implementation
 * @param expr
 */
    void CodeGenCPP::visit(mir::TensorStructReadExpr::Ptr expr) {
        //for dense array tensor read
        oss << expr->array_of_struct_target << "[";
        expr->index->accept(this);
        oss << "].";
        expr->field_target->accept(this);
        oss << " ";
    };


    void CodeGenCPP::visit(mir::VarExpr::Ptr expr) {
        oss << expr->var.getName();
    };

    void CodeGenCPP::visit(mir::EqExpr::Ptr expr) {
        oss << "(";
        expr->operands[0]->accept(this);
        oss << ")";

        for (unsigned i = 0; i < expr->ops.size(); ++i) {
            switch (expr->ops[i]) {
                case mir::EqExpr::Op::LT:
                    oss << " < ";
                    break;
                case mir::EqExpr::Op::LE:
                    oss << " <= ";
                    break;
                case mir::EqExpr::Op::GT:
                    oss << " > ";
                    break;
                case mir::EqExpr::Op::GE:
                    oss << " >= ";
                    break;
                case mir::EqExpr::Op::EQ:
                    oss << " == ";
                    break;
                case mir::EqExpr::Op::NE:
                    oss << " != ";
                    break;
                default:
                    break;
            }

            oss << "(";
            expr->operands[i + 1]->accept(this);
            oss << ")";
        }
    }

    void CodeGenCPP::visit(mir::MulExpr::Ptr expr) {
        oss << '(';
        expr->lhs->accept(this);
        oss << " * ";
        expr->rhs->accept(this);
        oss << ')';
    }

    void CodeGenCPP::visit(mir::DivExpr::Ptr expr) {
        oss << '(';
        expr->lhs->accept(this);
        oss << " / ";
        expr->rhs->accept(this);
        oss << ')';
    }

    void CodeGenCPP::visit(mir::AddExpr::Ptr expr) {
        oss << '(';
        expr->lhs->accept(this);
        oss << " + ";
        expr->rhs->accept(this);
        oss << ')';
    };

    void CodeGenCPP::visit(mir::SubExpr::Ptr expr) {
        oss << '(';
        expr->lhs->accept(this);
        oss << " - ";
        expr->rhs->accept(this);
        oss << ')';
    };

    void CodeGenCPP::visit(mir::BoolLiteral::Ptr expr) {
        oss << "(bool) ";
        oss << (bool) expr->val;
    };

    void CodeGenCPP::visit(mir::StringLiteral::Ptr expr) {
        oss << "\"";
        oss << expr->val;
        oss << "\"";
    };

    void CodeGenCPP::visit(mir::FloatLiteral::Ptr expr) {
        oss << "(";
        oss << "(float) ";
        oss << expr->val;
        oss << ") ";
    };

    void CodeGenCPP::visit(mir::IntLiteral::Ptr expr) {
        oss << "(";
        //oss << "(int) ";
        oss << expr->val;
        oss << ") ";
    }

    // Materialize the Element Data
    void CodeGenCPP::genElementData() {
        for (auto const &element_type_entry : mir_context_->properties_map_) {
            // for each element type
            for (auto const &var_decl : *element_type_entry.second) {
                // for each field / system vector of the element
                // generate just array implementation for now
                genPropertyArrayImplementationWithInitialization(var_decl);
            }
        }

    };

    void CodeGenCPP::genScalarDecl(mir::VarDecl::Ptr var_decl){
        //the declaration and the value are separate. The value is generated as a separate assign statement in the main function
        var_decl->type->accept(this);
        oss << var_decl->name << "; " << std::endl;
    }


    void CodeGenCPP::genScalarAlloc(mir::VarDecl::Ptr var_decl) {

        printIndent();

        oss << var_decl->name << " ";
        if (var_decl->initVal != nullptr) {
            oss << "= ";
            var_decl->initVal->accept(this);
        }
        oss << ";" << std::endl;

    }

    void CodeGenCPP::genPropertyArrayDecl(mir::VarDecl::Ptr var_decl) {
        // read the name of the array
        const auto name = var_decl->name;

        // read the type of the array
        mir::VectorType::Ptr vector_type = std::dynamic_pointer_cast<mir::VectorType>(var_decl->type);
        assert(vector_type != nullptr);
        auto vector_element_type = vector_type->vector_element_type;
        assert(vector_element_type != nullptr);

        /**  Deprecated, now we generate an array declaration, not a vector one
        //generate std::vector implementation
        oss << "std::vector< ";
        vector_element_type->accept(this);
        // pointer declaration
        oss << " >  ";
        oss << name;
        oss << ";" << std::endl;
         **/

        if (!mir::isa<mir::VectorType>(vector_element_type)){
            vector_element_type->accept(this);
            oss << " * __restrict " << name << ";" << std::endl;
        } else if (mir::isa<mir::VectorType>(vector_element_type)) {
            //if each element is a vector
            auto vector_vector_element_type = mir::to<mir::VectorType>(vector_element_type);
            assert(vector_vector_element_type->range_indexset != 0);
            int range = vector_vector_element_type->range_indexset;


            //std::string typedef_name = "defined_type_" + mir_context_->getUniqueNameCounterString();
            std::string typedef_name = vector_vector_element_type->toString();
            if (mir_context_->defined_types.find(typedef_name) == mir_context_->defined_types.end()){
                mir_context_->defined_types.insert(typedef_name);
                //first generates a typedef for the vector type
                oss << "typedef ";
                vector_vector_element_type->vector_element_type->accept(this);
                oss << typedef_name <<  " ";
                oss << "[ " << range << "]; " << std::endl;
            }

            vector_vector_element_type->typedef_name_ = typedef_name;


            //use the typedef defined type to declare a new pointer
            oss << typedef_name << " * __restrict  " << name << ";" << std::endl;

        } else {
            std::cout << "unsupported type for property: " << var_decl->name << std::endl;
            exit(0);
        }
    }


    void CodeGenCPP::genPropertyArrayAlloc(mir::VarDecl::Ptr var_decl) {
        const auto name = var_decl->name;
        printIndent();
        oss << name;
        // read the size of the array
        mir::VectorType::Ptr vector_type = std::dynamic_pointer_cast<mir::VectorType>(var_decl->type);
        const auto size_expr = mir_context_->getElementCount(vector_type->element_type);
        auto vector_element_type = vector_type->vector_element_type;

        assert(size_expr != nullptr);

        /** Deprecated, now we uses a "new" allocation scheme for arrays
        oss << " = std::vector< ";
        vector_element_type->accept(this);
        oss << " >  ";
        oss << " ( ";
        size_expr->accept(this);
        oss << " ); " << std::endl;
         **/

        oss << " = new ";

        if (mir::isa<mir::VectorType>(vector_element_type)){
            //for vector type, we use the name from typedef
            auto vector_type_vector_element_type = mir::to<mir::VectorType>(vector_element_type);
            assert(vector_type_vector_element_type->typedef_name_ != "");
            oss << vector_type_vector_element_type->typedef_name_ << " ";
        } else {
            vector_element_type->accept(this);
        }

        oss << "[ ";
        size_expr -> accept(this);
        oss << "];" << std::endl;
    }

    void CodeGenCPP::genPropertyArrayImplementationWithInitialization(mir::VarDecl::Ptr var_decl) {
        // read the name of the array
        const auto name = var_decl->name;

        // read the type of the array
        mir::VectorType::Ptr vector_type = std::dynamic_pointer_cast<mir::VectorType>(var_decl->type);
        assert(vector_type != nullptr);
        auto vector_element_type = vector_type->vector_element_type;
        assert(vector_element_type != nullptr);

        //generate std::vector implementation
        oss << "std::vector< ";
        vector_element_type->accept(this);
        // pointer declaration
        oss << " >  ";
        oss << name;

        // read the size of the array
        const auto size_expr = mir_context_->getElementCount(vector_type->element_type);
        assert(size_expr != nullptr);
        const auto init_val = var_decl->initVal;

        if (std::dynamic_pointer_cast<mir::Call>(init_val)) {
            auto call_expr = std::dynamic_pointer_cast<mir::Call>(init_val);
            oss << " = ";
            call_expr->accept(this);
            oss << ";" << std::endl;

        } else {
            oss << " ( ";
            size_expr->accept(this);
            if (init_val) {
                // struct types don't have initial values
                oss << " , ";
                init_val->accept(this);
            }
            oss << " ); " << std::endl;

        }
    }

    void CodeGenCPP::visit(mir::ElementType::Ptr element_type) {
        //currently, we generate an index id into the vectors
        oss << "NodeID ";
    }

    void CodeGenCPP::visit(mir::VertexSetAllocExpr::Ptr alloc_expr) {
        oss << "new VertexSubset<int> ( ";
        //This is the current number of elements, but we need the range
        //alloc_expr->size_expr->accept(this);
        const auto size_expr = mir_context_->getElementCount(alloc_expr->element_type);
        size_expr->accept(this);
        oss << " , ";
        alloc_expr->size_expr->accept(this);
        oss << ")";
    }

    void CodeGenCPP::visit(mir::ListAllocExpr::Ptr alloc_expr) {
        oss << "new std::vector< ";
        alloc_expr->element_type->accept(this);
        oss << " > ( ";
        // currently we don't support initializing a vector with size
        //This is the current number of elements, but we need the range
        //alloc_expr->size_expr->accept(this);
        //const auto size_expr = mir_context_->getElementCount(alloc_expr->element_type);
        //size_expr->accept(this);
        //oss << " , ";
        //alloc_expr->size_expr->accept(this);
        oss << ")";
    }


    void CodeGenCPP::visit(mir::VertexSetApplyExpr::Ptr apply_expr) {
        //vertexset apply
        auto mir_var = std::dynamic_pointer_cast<mir::VarExpr>(apply_expr->target);

        if (mir_context_->isConstVertexSet(mir_var->var.getName())){
            //if the verstexset is a const / global vertexset, then we can get size easily
            auto associated_element_type = mir_context_->getElementTypeFromVectorOrSetName(mir_var->var.getName());
            assert(associated_element_type);
            auto associated_element_type_size = mir_context_->getElementCount(associated_element_type);
            assert(associated_element_type_size);
            std::string for_type = apply_expr->is_parallel ? "parallel_for" : "for";
            oss << for_type << " (int vertexsetapply_iter = 0; vertexsetapply_iter < ";
            associated_element_type_size->accept(this);
            oss << "; vertexsetapply_iter++) {" << std::endl;
            indent();
            printIndent();
            if (mir_context_->isExternFunction(apply_expr->input_function_name)){
                // This function is an extern function (not a functor)
                oss << apply_expr->input_function_name << "(vertexsetapply_iter);" << std::endl;
            } else  {
                // This function is not an extern function, it is defined in GraphIt code
                // This would generate a functor declaration
                oss << apply_expr->input_function_name << "()(vertexsetapply_iter);" << std::endl;
            }
            dedent();
            printIndent();
            oss << "}";
        } else {
            // NOT sure what how this condition is triggered and used
            // if this is a dynamically created vertexset
            oss << " builtin_vertexset_apply ( " << mir_var->var.getName() << ", ";
            oss << apply_expr->input_function_name << "() ); " << std::endl;


        }


    }

    void CodeGenCPP::visit(mir::PullEdgeSetApplyExpr::Ptr apply_expr) {

        // get the name of the function declaration
        //edgeset apply
        auto mir_var = std::dynamic_pointer_cast<mir::VarExpr>(apply_expr->target);
        // DEPRECATED, we now do it at various statements (assign)
        // TODO: fix using this visitor again, so we don't need to do it at statements
        //generate code for pull edgeset apply
        //genEdgeSetPullApply(mir_var, apply_expr);
    }

    void CodeGenCPP::visit(mir::PushEdgeSetApplyExpr::Ptr apply_expr) {
        // generate code for push edgeset apply
        // DEPRECATED, we now do it at various statements (assign)
        // TODO: fix using this visitor again, so we don't need to do it at statements
        //genEdgesetPushApply(apply_expr);
    }

    void CodeGenCPP::visit(mir::VertexSetWhereExpr::Ptr vertexset_where_expr) {

	// Removing all this code to just generate calls to builtin
        //dense vertex set apply
/*
        if (vertexset_where_expr->is_constant_set) {
            auto associated_element_type =
                    mir_context_->getElementTypeFromVectorOrSetName(vertexset_where_expr->target);
            assert(associated_element_type);
            auto associated_element_type_size = mir_context_->getElementCount(associated_element_type);
            assert(associated_element_type_size);
            oss << "auto ____graphit_tmp_out = new VertexSubset <NodeID> ( ";

            //get the total number of vertices in the vertex set
            auto vertex_type = mir_context_->getElementTypeFromVectorOrSetName(vertexset_where_expr->target);
            auto vertices_range_expr =
                    mir_context_->getElementCount(vertex_type);
            vertices_range_expr->accept(this);
            oss << " , ";
            //vertices_range_expr->accept(this);
            // the output vertexset is initially set to 0
            oss << "0";
            oss << " );" << std::endl;
            std::string next_bool_map_name = "next" + mir_context_->getUniqueNameCounterString();
            oss << "bool * " << next_bool_map_name << " = newA(bool, ";
            vertices_range_expr->accept(this);
            oss << ");\n";
            printIndent();
            oss << "parallel_for (int v = 0; v < ";
            associated_element_type_size->accept(this);
            oss << "; v++) {" << std::endl;
            indent();
            printIndent();
            oss << next_bool_map_name << "[v] = 0;" << std::endl;
            oss << "if ( " << vertexset_where_expr->input_func << "()( v ) )" << std::endl;
            indent();
            printIndent();
            oss << next_bool_map_name << "[v] = 1;" << std::endl;
            dedent();
            dedent();
            printIndent();
            oss << "} //end of loop\n";
            oss << "____graphit_tmp_out->num_vertices_ = sequence::sum( "
                <<  next_bool_map_name << ", " ;
            vertices_range_expr->accept(this);
            oss << " );\n"
                    "____graphit_tmp_out->bool_map_ = ";
            oss << next_bool_map_name << ";\n";
        }
*/
        if (vertexset_where_expr->is_constant_set) {

            auto associated_element_type =
                    mir_context_->getElementTypeFromVectorOrSetName(vertexset_where_expr->target);
            auto associated_element_type_size = mir_context_->getElementCount(associated_element_type);
            oss << "builtin_const_vertexset_filter <";
	    oss << vertexset_where_expr->input_func ;
            oss << ">(";
            oss << vertexset_where_expr->input_func << "(), ";
            associated_element_type_size->accept(this);
            oss << ")";
        } else {
            oss << "builtin_vertexset_filter <";
            oss << vertexset_where_expr->input_func;
            oss << ">(";
            oss << vertexset_where_expr->target << ", " << vertexset_where_expr->input_func << "()";
            oss << ")";
        }
    }

    void CodeGenCPP::genEdgeSets() {
        for (auto edgeset : mir_context_->getEdgeSets()) {

            auto edge_set_type = mir::to<mir::EdgeSetType>(edgeset->type);
            edge_set_type->accept(this);
            oss << edgeset->name << ";" << std::endl;

            // Deprecated code
//            if (edge_set_type->weight_type != nullptr) {
//                //weighted edgeset
//                //unweighted edgeset
//                oss << "WGraph " << edgeset->name << ";" << std::endl;
//            } else {
//                //unweighted edgeset
//                oss << "Graph " << edgeset->name << "; " << std::endl;
//            }
        }
    }


    /**
     * Generate the struct types before the arrays are generated
     */
    void CodeGenCPP::genStructTypeDecls() {
        for (auto const &struct_type_decl_entry : mir_context_->struct_type_decls) {
            auto struct_type_decl = struct_type_decl_entry.second;
            oss << "typedef struct ";
            oss << struct_type_decl->name << " { " << std::endl;

            for (auto var_decl : struct_type_decl->fields) {
                indent();
                printIndent();
                var_decl->type->accept(this);
                //we don't initialize in the struct declarations anymore
                // the initializations are done in the main function
                oss << var_decl->name;
                // << " = ";
                //var_decl->initVal->accept(this);
                oss << ";" << std::endl;
                dedent();
            }
            oss << "} " << struct_type_decl->name << ";" << std::endl;
        }
    }

    void CodeGenCPP::visit(mir::VertexSetType::Ptr vertexset_type) {
        oss << "VertexSubset<int> *  ";
    }

    void CodeGenCPP::visit(mir::ListType::Ptr list_type) {
        oss << "std::vector< ";
        list_type->element_type->accept(this);
        oss << " > *  ";
    }

    void CodeGenCPP::visit(mir::NegExpr::Ptr neg_expr) {
        if (neg_expr->negate) oss << " -";
        neg_expr->operand->accept(this);
    }

    void CodeGenCPP::genEdgesetApplyFunctionCall(mir::EdgeSetApplyExpr::Ptr apply) {
        // the arguments order here has to be consistent with genEdgeApplyFunctionSignature in gen_edge_apply_func_decl.cpp

        auto edgeset_apply_func_name = edgeset_apply_func_gen_->genFunctionName(apply);
        oss << edgeset_apply_func_name << "(";
        auto mir_var = std::dynamic_pointer_cast<mir::VarExpr>(apply->target);
        std::vector<std::string> arguments = std::vector<std::string>();


        if (apply->from_func != "") {
            if (mir_context_->isFunction(apply->from_func)) {
                // the schedule is an input from function
                // Create functor instance
                arguments.push_back(genFuncNameAsArgumentString(apply->from_func));
            } else {
                // the input is an input from vertexset
                arguments.push_back(apply->from_func);
            }
        }

        if (apply->to_func != "") {
            if (mir_context_->isFunction(apply->to_func)) {
                // the schedule is an input to function
                // Create functor instance
                arguments.push_back(genFuncNameAsArgumentString(apply->to_func));
            } else {
                // the input is an input to vertexset
                arguments.push_back(apply->to_func);
            }
        }

        // the original apply function (pull direction in hybrid case)
        arguments.push_back(genFuncNameAsArgumentString(apply->input_function_name));

        // a filter function for the push direction in hybrid code
        if (mir::isa<mir::HybridDenseEdgeSetApplyExpr>(apply)){
            auto apply_expr = mir::to<mir::HybridDenseEdgeSetApplyExpr>(apply);
            if (apply_expr->push_to_function_ != ""){
                arguments.push_back(genFuncNameAsArgumentString(apply_expr->push_to_function_));
            }
        }

        // the push direction apply function for hybrid schedule
        if (mir::isa<mir::HybridDenseEdgeSetApplyExpr>(apply)){
            auto apply_expr = mir::to<mir::HybridDenseEdgeSetApplyExpr>(apply);
            arguments.push_back(genFuncNameAsArgumentString(apply_expr->push_function_));
        }

        // the edgeset that is being applied over (target)
        apply->target->accept(this);
        for (auto &arg : arguments) {
            oss << ", " << arg;
        }

        oss << "); " << std::endl;
    }

    void CodeGenCPP::visit(mir::EdgeSetLoadExpr::Ptr edgeset_load_expr) {
        if (edgeset_load_expr->is_weighted_){
            oss << "builtin_loadWeightedEdgesFromFile ( ";
            edgeset_load_expr->file_name->accept(this);
            oss << ") ";
        } else {
            oss << "builtin_loadEdgesFromFile ( ";
            edgeset_load_expr->file_name->accept(this);
            oss << ") ";
        }
    }

    void CodeGenCPP::visit(mir::EdgeSetType::Ptr edgeset_type) {
        if (edgeset_type->weight_type != nullptr) {
            //weighted edgeset
            //unweighted edgeset
            oss << "WGraph ";
        } else {
            //unweighted edgeset
            oss << "Graph ";
        }
    }

    void CodeGenCPP::visit(mir::VectorAllocExpr::Ptr alloc_expr) {
        oss << "new ";

        if (alloc_expr->scalar_type != nullptr){
            alloc_expr->scalar_type->accept(this);
        } else if (alloc_expr->vector_type != nullptr){
            oss << alloc_expr->vector_type->toString();
        }
        oss << "[ ";
        //This is the current number of elements, but we need the range
        //alloc_expr->size_expr->accept(this);
        const auto size_expr = mir_context_->getElementCount(alloc_expr->element_type);
        if (size_expr != nullptr)
            size_expr->accept(this);
	else {
	    // This means it is a vector of constant size. The size_expr now directly holds the constant literal.
	    alloc_expr->size_expr->accept(this);
	}
        oss << "]";
    }

    std::string CodeGenCPP::genFuncNameAsArgumentString(std::string func_name) {
        if (mir_context_->isExternFunction(func_name)){
            //If it is an extern function, don't need to do anything, just pass the func name
            return func_name;
        } else {
            //If it is a GraphIt generated function, then we need to instantiate the functor
            return func_name + "()";
        }
    }

    void CodeGenCPP::genTypesRequiringTypeDefs() {

        for (mir::Type::Ptr type : mir_context_->types_requiring_typedef){
            if(mir::isa<mir::VectorType>(type)){
                auto vector_type = mir::to<mir::VectorType>(type);
                int range = vector_type->range_indexset;
                std::string typedef_name = vector_type->toString();
                if (mir_context_->defined_types.find(typedef_name) == mir_context_->defined_types.end()){
                    mir_context_->defined_types.insert(typedef_name);
                    //first generates a typedef for the vector type
                    oss << "typedef ";
                    vector_type->vector_element_type->accept(this);
                    oss << typedef_name <<  " ";
                    oss << "[ " << range << "]; " << std::endl;
                }
            }
        }
    }
}
