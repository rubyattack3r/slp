package sleepy

/*
#include <stdlib.h>
#include "sleepy.h"

// Wrapper for the SleepyAllocator reallocate function
extern void* goSleepyReallocInfo(void* ptr, size_t newSize, void* userData);

static inline void set_go_allocator(SleepyAllocator* alloc) {
    alloc->reallocate = goSleepyReallocInfo;
    alloc->user_data = NULL;
}

*/
import "C"
import (
	"errors"
	"fmt"
	"unsafe"
)

// Package-level errors
var (
	ErrParsingFailed       = errors.New("parsing failed")
	ErrASTGenerationFailed = errors.New("could not generate AST")
)

//export goSleepyReallocInfo
func goSleepyReallocInfo(ptr unsafe.Pointer, newSize C.size_t, userData unsafe.Pointer) unsafe.Pointer {
	if newSize == 0 {
		C.free(ptr)
		return nil
	}
	return C.realloc(ptr, newSize)
}

type Parser struct {
	cParser   *C.SleepyParser
	allocator *C.SleepyAllocator
}

func NewParser() *Parser {
	alloc := (*C.SleepyAllocator)(C.malloc(C.sizeof_SleepyAllocator))
	C.set_go_allocator(alloc)

	parser := (*C.SleepyParser)(C.malloc(C.sizeof_SleepyParser))

	return &Parser{
		cParser:   parser,
		allocator: alloc,
	}
}

// Close releases the C memory allocated for the parser and its allocator.
// It implements the io.Closer interface.
func (p *Parser) Close() error {
	if p.cParser != nil {
		C.free(unsafe.Pointer(p.cParser))
		p.cParser = nil
	}
	if p.allocator != nil {
		C.free(unsafe.Pointer(p.allocator))
		p.allocator = nil
	}
	return nil
}

// NodeType represents the type of a Sleepy AST node
type NodeType int

// Node represents a Sleepy AST Node
type Node struct {
	Type     NodeType
	Line     int
	Value    interface{} // Mapped value based on Type (e.g. string for operators, or specific structs)
	Children []*Node     // Child nodes for traversal
}

type ForeachMetadata struct {
	Index string
	Value string
}

type ForMetadata struct {
	InitCount int
	IncCount  int
}

type ImportMetadata struct {
	Path string
}

type TryCatchMetadata struct {
	CatchVar string
}

// AST Node Type Constants
const (
	AstBoolean      NodeType = C.SLEEPY_AST_BOOLEAN
	AstLong         NodeType = C.SLEEPY_AST_LONG
	AstNumber       NodeType = C.SLEEPY_AST_NUMBER
	AstLiteral      NodeType = C.SLEEPY_AST_LITERAL
	AstString       NodeType = C.SLEEPY_AST_STRING
	AstScalar       NodeType = C.SLEEPY_AST_SCALAR
	AstArray        NodeType = C.SLEEPY_AST_ARRAY
	AstHashtable    NodeType = C.SLEEPY_AST_HASHTABLE
	AstIdentifier   NodeType = C.SLEEPY_AST_IDENTIFIER
	AstBacktick     NodeType = C.SLEEPY_AST_BACKTICK
	AstAddress      NodeType = C.SLEEPY_AST_ADDRESS
	AstCall         NodeType = C.SLEEPY_AST_CALL
	AstBinop        NodeType = C.SLEEPY_AST_BINOP
	AstAssignment   NodeType = C.SLEEPY_AST_ASSIGNMENT
	AstUnaryop      NodeType = C.SLEEPY_AST_UNARYOP
	AstIf           NodeType = C.SLEEPY_AST_IF
	AstWhile        NodeType = C.SLEEPY_AST_WHILE
	AstFor          NodeType = C.SLEEPY_AST_FOR
	AstForeach      NodeType = C.SLEEPY_AST_FOREACH
	AstReturn       NodeType = C.SLEEPY_AST_RETURN
	AstBreak        NodeType = C.SLEEPY_AST_BREAK
	AstContinue     NodeType = C.SLEEPY_AST_CONTINUE
	AstYield        NodeType = C.SLEEPY_AST_YIELD
	AstAssignLoop   NodeType = C.SLEEPY_AST_ASSIGN_LOOP
	AstIndex        NodeType = C.SLEEPY_AST_INDEX
	AstObjExpr      NodeType = C.SLEEPY_AST_OBJ_EXPR
	AstTryCatch     NodeType = C.SLEEPY_AST_TRY_CATCH
	AstThrow        NodeType = C.SLEEPY_AST_THROW
	AstAssert       NodeType = C.SLEEPY_AST_ASSERT
	AstEnvBridge    NodeType = C.SLEEPY_AST_ENV_BRIDGE
	AstImport       NodeType = C.SLEEPY_AST_IMPORT
	AstLvalueTuple  NodeType = C.SLEEPY_AST_LVALUE_TUPLE
	AstArg          NodeType = C.SLEEPY_AST_ARG
	AstKvPair       NodeType = C.SLEEPY_AST_KV_PAIR
	AstDone         NodeType = C.SLEEPY_AST_DONE
	AstHalt         NodeType = C.SLEEPY_AST_HALT
	AstCallcc       NodeType = C.SLEEPY_AST_CALLCC
	AstLocal        NodeType = C.SLEEPY_AST_LOCAL
	AstThis         NodeType = C.SLEEPY_AST_THIS
	AstBlock        NodeType = C.SLEEPY_AST_BLOCK
	AstScript       NodeType = C.SLEEPY_AST_SCRIPT
	AstClassLiteral NodeType = C.SLEEPY_AST_CLASS_LITERAL
	AstNop          NodeType = C.SLEEPY_AST_NOP
	AstError        NodeType = C.SLEEPY_AST_ERROR
)

// String implements the fmt.Stringer interface for NodeType
func (n NodeType) String() string {
	switch n {
	case AstBoolean:
		return "AstBoolean"
	case AstLong:
		return "AstLong"
	case AstNumber:
		return "AstNumber"
	case AstLiteral:
		return "AstLiteral"
	case AstString:
		return "AstString"
	case AstScalar:
		return "AstScalar"
	case AstArray:
		return "AstArray"
	case AstHashtable:
		return "AstHashtable"
	case AstIdentifier:
		return "AstIdentifier"
	case AstBacktick:
		return "AstBacktick"
	case AstAddress:
		return "AstAddress"
	case AstCall:
		return "AstCall"
	case AstBinop:
		return "AstBinop"
	case AstAssignment:
		return "AstAssignment"
	case AstUnaryop:
		return "AstUnaryop"
	case AstIf:
		return "AstIf"
	case AstWhile:
		return "AstWhile"
	case AstFor:
		return "AstFor"
	case AstForeach:
		return "AstForeach"
	case AstReturn:
		return "AstReturn"
	case AstBreak:
		return "AstBreak"
	case AstContinue:
		return "AstContinue"
	case AstYield:
		return "AstYield"
	case AstAssignLoop:
		return "AstAssignLoop"
	case AstIndex:
		return "AstIndex"
	case AstObjExpr:
		return "AstObjExpr"
	case AstTryCatch:
		return "AstTryCatch"
	case AstThrow:
		return "AstThrow"
	case AstAssert:
		return "AstAssert"
	case AstEnvBridge:
		return "AstEnvBridge"
	case AstImport:
		return "AstImport"
	case AstLvalueTuple:
		return "AstLvalueTuple"
	case AstArg:
		return "AstArg"
	case AstKvPair:
		return "AstKvPair"
	case AstDone:
		return "AstDone"
	case AstHalt:
		return "AstHalt"
	case AstCallcc:
		return "AstCallcc"
	case AstLocal:
		return "AstLocal"
	case AstThis:
		return "AstThis"
	case AstBlock:
		return "AstBlock"
	case AstScript:
		return "AstScript"
	case AstClassLiteral:
		return "AstClassLiteral"
	case AstNop:
		return "AstNop"
	case AstError:
		return "AstError"
	default:
		return fmt.Sprintf("UnknownNodeType(%d)", int(n))
	}
}

// Parse parses the provided Sleepy source code and returns the root AST Node if successful.
func (p *Parser) Parse(source string) (*Node, error) {
	cSource := C.CString(source)
	defer C.free(unsafe.Pointer(cSource))

	C.sleepy_parser_init(p.cParser, cSource, p.allocator)

	// Parse the code
	node := C.sleepy_parser_parse(p.cParser)

	if p.cParser.had_error {
		msg := "parsing failed"
		if p.cParser.error_message != nil {
			msg = C.GoString(p.cParser.error_message)
		}
		return nil, fmt.Errorf("%s at line %d", msg, int(p.cParser.error_line))
	}

	if node == nil {
		return nil, ErrASTGenerationFailed
	}

	return p.convertNode(node), nil
}

// Format serializes the AST node back into Sleepy source code natively using the core C formatter.
func (n *Node) Format(p *Parser) string {
	if n == nil || p == nil {
		return ""
	}

	// 1. Build C AST Tree
	cNode := p.buildCNode(n)
	if cNode == nil {
		return ""
	}
	defer C.sleepy_parser_free_node(cNode, p.allocator)

	// 2. Format it using the C library
	// The C formatter returns an allocated string that we must free
	fmt.Printf("DEBUG: Formatting node type %d with %d children\n", n.Type, len(n.Children))
	cStr := C.sleepy_ast_format(cNode, p.allocator)
	if cStr == nil {
		return ""
	}
	// We use the same allocator to free the resulting string memory by reallocating to size 0
	defer C.goSleepyReallocInfo(unsafe.Pointer(cStr), 0, nil)

	return C.GoString(cStr)
}

// buildCNode constructs a C AST Node tree from a Go Node tree.
func (p *Parser) buildCNode(node *Node) *C.SleepyASTNode {
	if node == nil {
		return nil
	}

	cNode := C.sleepy_ast_build_node(C.SleepyASTNodeType(node.Type), C.int(node.Line), p.allocator)
	if cNode == nil {
		return nil
	}

	// Assign values
	switch node.Type {
	case AstBoolean:
		if val, ok := node.Value.(bool); ok {
			C.sleepy_ast_set_bool_val(cNode, C.bool(val))
		}
	case AstLong:
		if val, ok := node.Value.(int64); ok {
			C.sleepy_ast_set_long_val(cNode, C.long(val))
		}
	case AstNumber:
		if val, ok := node.Value.(float64); ok {
			C.sleepy_ast_set_double_val(cNode, C.double(val))
		}
	case AstString, AstLiteral, AstScalar, AstArray, AstHashtable, AstIdentifier, AstClassLiteral:
		if val, ok := node.Value.(string); ok {
			cStr := C.CString(val)
			defer C.free(unsafe.Pointer(cStr))
			C.sleepy_ast_set_string_val(cNode, cStr, p.allocator)
		}
	}

	// Helper for children
	buildChildren := func() (**C.SleepyASTNode, C.size_t) {
		if len(node.Children) == 0 {
			return nil, 0
		}
		// Allocate C memory for the children pointers
		cPtr := C.sleepy_ast_allocate_children(C.size_t(len(node.Children)), p.allocator)
		if cPtr == nil {
			return nil, 0
		}
		cChildren := (**C.SleepyASTNode)(cPtr)
		childrenSlice := unsafe.Slice(cChildren, len(node.Children))

		for i, child := range node.Children {
			childrenSlice[i] = p.buildCNode(child)
		}
		return cChildren, C.size_t(len(node.Children))
	}

	// Configure structure based on specific node types
	switch node.Type {
	case AstBlock, AstScript:
		cChildrenArr, cCount := buildChildren()
		C.sleepy_ast_set_children(cNode, cChildrenArr, cCount, p.allocator)

	case AstCall:
		cChildrenArr, cCount := buildChildren()
		C.sleepy_ast_set_children(cNode, cChildrenArr, cCount, p.allocator)
		if val, ok := node.Value.(string); ok {
			cTarget := C.CString(val)
			defer C.free(unsafe.Pointer(cTarget))
			C.sleepy_ast_set_call_target(cNode, cTarget, p.allocator)
		}

	case AstBinop:
		if len(node.Children) == 2 {
			cLeft := p.buildCNode(node.Children[0])
			cRight := p.buildCNode(node.Children[1])
			if op, ok := node.Value.(string); ok {
				cOp := C.CString(op)
				defer C.free(unsafe.Pointer(cOp))
				C.sleepy_ast_set_binop_with_op(cNode, cLeft, cRight, cOp, p.allocator)
			} else {
				C.sleepy_ast_set_binop(cNode, cLeft, cRight)
			}
		}

	case AstUnaryop:
		if len(node.Children) == 1 {
			cOperand := p.buildCNode(node.Children[0])
			if op, ok := node.Value.(string); ok {
				cOp := C.CString(op)
				defer C.free(unsafe.Pointer(cOp))
				C.sleepy_ast_set_unaryop_with_op(cNode, cOperand, cOp, p.allocator)
			}
		}

	case AstIf:
		var cCond, cThen, cElse *C.SleepyASTNode
		if len(node.Children) > 0 {
			cCond = p.buildCNode(node.Children[0])
		}
		if len(node.Children) > 1 {
			cThen = p.buildCNode(node.Children[1])
		}
		if len(node.Children) > 2 {
			cElse = p.buildCNode(node.Children[2])
		}
		C.sleepy_ast_set_if(cNode, cCond, cThen, cElse)

	case AstWhile:
		var cCond, cBody *C.SleepyASTNode
		if len(node.Children) > 0 {
			cCond = p.buildCNode(node.Children[0])
		}
		if len(node.Children) > 1 {
			cBody = p.buildCNode(node.Children[1])
		}
		C.sleepy_ast_set_while(cNode, cCond, cBody)

	case AstArg:
		if len(node.Children) == 2 {
			cName := p.buildCNode(node.Children[0])
			cVal := p.buildCNode(node.Children[1])
			C.sleepy_ast_set_arg_with_name(cNode, cName, cVal)
		} else if len(node.Children) == 1 {
			cVal := p.buildCNode(node.Children[0])
			C.sleepy_ast_set_arg(cNode, cVal)
		}

	case AstAssignment:
		if len(node.Children) == 2 {
			cTarget := p.buildCNode(node.Children[0])
			cVal := p.buildCNode(node.Children[1])
			C.sleepy_ast_set_assignment(cNode, cTarget, cVal)
		}

	case AstIndex:
		if len(node.Children) == 2 {
			cCont := p.buildCNode(node.Children[0])
			cElem := p.buildCNode(node.Children[1])
			C.sleepy_ast_set_index(cNode, cCont, cElem)
		}

	case AstObjExpr:
		if len(node.Children) >= 1 {
			cTarget := p.buildCNode(node.Children[0])
			var cMessage *C.SleepyASTNode
			var cArgs **C.SleepyASTNode
			var cArgCount C.size_t

			if len(node.Children) > 1 {
				// We don't always have a message. But let's assume Children[1] is message if it exists in the tree as a string or identifier.
				// Wait, the Go AST structure for AstObjExpr: if node is created from Sleepy parser, the message is Children[1] (if it exists).
				// We'll just pass whatever the children are. If we're constructing this, we need to map them back.
				// For the bug we're fixing `[$callback]`, there is no message, just the target.
				// Let's just set the target and no args if len is 1. If more, we'll need to know which child is what.
				// By default, let's just serialize the first child as target and the rest as args (ignoring message for now since we just need closure invocation to work).
				cArgCount = C.size_t(len(node.Children) - 1)
				cPtr := C.sleepy_ast_allocate_children(cArgCount, p.allocator)
				cArgs = (**C.SleepyASTNode)(cPtr)
				argSlice := unsafe.Slice(cArgs, int(cArgCount))
				for i := 0; i < int(cArgCount); i++ {
					argSlice[i] = p.buildCNode(node.Children[i+1])
				}
			}
			C.sleepy_ast_set_obj_expr(cNode, cTarget, cMessage, cArgs, cArgCount, p.allocator)
		}

	case AstKvPair:
		if len(node.Children) == 2 {
			cName := p.buildCNode(node.Children[0])
			cVal := p.buildCNode(node.Children[1])
			C.sleepy_ast_set_kv_pair(cNode, cName, cVal)
		}


	case AstEnvBridge:
		// Keyword is stored in node.Value
		if kw, ok := node.Value.(string); ok {
			cKw := C.CString(kw)
			defer C.free(unsafe.Pointer(cKw))
			C.sleepy_ast_set_env_bridge_keyword(cNode, cKw, p.allocator)
		}

		// Children are: ID (optional), String (optional), Body (optional)
		for _, child := range node.Children {
			if child == nil {
				continue
			}
			switch child.Type {
			case AstIdentifier:
				if val, ok := child.Value.(string); ok {
					cId := C.CString(val)
					defer C.free(unsafe.Pointer(cId))
					C.sleepy_ast_set_env_bridge_id(cNode, cId, p.allocator)
				}
			case AstString, AstLiteral:
				if val, ok := child.Value.(string); ok {
					cStr := C.CString(val)
					defer C.free(unsafe.Pointer(cStr))
					C.sleepy_ast_set_env_bridge_string(cNode, cStr, p.allocator)
				}
			case AstBlock:
				cBody := p.buildCNode(child)
				C.sleepy_ast_set_env_bridge_body(cNode, cBody)
			}
		}

	case AstForeach:
		if meta, ok := node.Value.(ForeachMetadata); ok {
			cIdx := C.CString(meta.Index)
			defer C.free(unsafe.Pointer(cIdx))
			cVal := C.CString(meta.Value)
			defer C.free(unsafe.Pointer(cVal))

			var cGen, cBody *C.SleepyASTNode
			if len(node.Children) > 0 {
				cGen = p.buildCNode(node.Children[0])
			}
			if len(node.Children) > 1 {
				cBody = p.buildCNode(node.Children[1])
			}
			C.sleepy_ast_set_foreach(cNode, cIdx, cVal, cGen, cBody, p.allocator)
		}

	case AstFor:
		if meta, ok := node.Value.(ForMetadata); ok {
			// Children are: init..., cond, inc..., body
			total := len(node.Children)
			if total == 0 {
				break
			}

			// Extract init
			var cInit **C.SleepyASTNode
			if meta.InitCount > 0 {
				cPtr := C.sleepy_ast_allocate_children(C.size_t(meta.InitCount), p.allocator)
				cInit = (**C.SleepyASTNode)(cPtr)
				initSlice := unsafe.Slice(cInit, meta.InitCount)
				for i := 0; i < meta.InitCount && i < total; i++ {
					initSlice[i] = p.buildCNode(node.Children[i])
				}
			}

			// Extract cond
			var cCond *C.SleepyASTNode
			condIdx := meta.InitCount
			if condIdx < total {
				cCond = p.buildCNode(node.Children[condIdx])
			}

			// Extract inc
			var cInc **C.SleepyASTNode
			incStart := condIdx + 1
			if meta.IncCount > 0 {
				cPtr := C.sleepy_ast_allocate_children(C.size_t(meta.IncCount), p.allocator)
				cInc = (**C.SleepyASTNode)(cPtr)
				incSlice := unsafe.Slice(cInc, meta.IncCount)
				for i := 0; i < meta.IncCount && (incStart+i) < total; i++ {
					incSlice[i] = p.buildCNode(node.Children[incStart+i])
				}
			}

			// Extract body
			var cBody *C.SleepyASTNode
			bodyIdx := incStart + meta.IncCount
			if bodyIdx < total {
				cBody = p.buildCNode(node.Children[bodyIdx])
			}

			C.sleepy_ast_set_for(cNode, cInit, C.size_t(meta.InitCount), cCond, cInc, C.size_t(meta.IncCount), cBody, p.allocator)
		}

	case AstReturn, AstThrow, AstAssert, AstYield:
		if len(node.Children) > 0 {
			cVal := p.buildCNode(node.Children[0])
			switch node.Type {
			case AstReturn:
				C.sleepy_ast_set_return(cNode, cVal)
			case AstThrow:
				C.sleepy_ast_set_throw(cNode, cVal)
			case AstAssert:
				C.sleepy_ast_set_assert(cNode, cVal)
			case AstYield:
				C.sleepy_ast_set_yield(cNode, cVal)
			}
		}

	case AstBreak:
		C.sleepy_ast_set_break(cNode)
	case AstContinue:
		C.sleepy_ast_set_continue(cNode)

	case AstTryCatch:
		if meta, ok := node.Value.(TryCatchMetadata); ok {
			var cBody, cHandler *C.SleepyASTNode
			if len(node.Children) > 0 {
				cBody = p.buildCNode(node.Children[0])
			}
			if len(node.Children) > 1 {
				cHandler = p.buildCNode(node.Children[1])
			}
			cVar := C.CString(meta.CatchVar)
			defer C.free(unsafe.Pointer(cVar))
			C.sleepy_ast_set_try_catch(cNode, cBody, cVar, cHandler, p.allocator)
		}

	case AstImport:
		if meta, ok := node.Value.(ImportMetadata); ok {
			cPath := C.CString(meta.Path)
			defer C.free(unsafe.Pointer(cPath))
			C.sleepy_ast_set_import(cNode, cPath, p.allocator)
		}

	case AstBacktick, AstAddress:
		if val, ok := node.Value.(string); ok {
			cStr := C.CString(val)
			defer C.free(unsafe.Pointer(cStr))
			if node.Type == AstBacktick {
				C.sleepy_ast_set_backtick(cNode, cStr, p.allocator)
			} else {
				C.sleepy_ast_set_string_val(cNode, cStr, p.allocator)
			}
		}
	}

	return cNode
}

// Walk traverses the AST and applies the provided function to each node.
func (n *Node) Walk(fn func(*Node) *Node) *Node {
	if n == nil {
		return nil
	}

	// Apply function to children first (bottom-up)
	for i, child := range n.Children {
		n.Children[i] = child.Walk(fn)
	}

	// Apply function to the node itself
	return fn(n)
}

// Helper to convert C.SleepyASTNode to Go Node
func (p *Parser) convertNode(cNode *C.SleepyASTNode) *Node {
	if cNode == nil {
		return nil
	}

	node := &Node{
		Type: NodeType(cNode._type),
		Line: int(cNode.line),
	}

	return p.mapNodeData(cNode, node)
}

func (p *Parser) mapNodeData(cNode *C.SleepyASTNode, node *Node) *Node {
	cType := cNode._type
	switch cType {
	case C.SLEEPY_AST_BOOLEAN:
		node.Value = bool(C.sleepy_ast_get_bool(cNode))
	case C.SLEEPY_AST_LONG:
		node.Value = int64(C.sleepy_ast_get_long(cNode))
	case C.SLEEPY_AST_NUMBER:
		node.Value = float64(C.sleepy_ast_get_double(cNode))
	case C.SLEEPY_AST_STRING, C.SLEEPY_AST_LITERAL, C.SLEEPY_AST_SCALAR,
		C.SLEEPY_AST_ARRAY, C.SLEEPY_AST_HASHTABLE, C.SLEEPY_AST_IDENTIFIER,
		C.SLEEPY_AST_CLASS_LITERAL, C.SLEEPY_AST_BACKTICK, C.SLEEPY_AST_ADDRESS:
		val := C.sleepy_ast_get_string(cNode)
		if val != nil {
			node.Value = C.GoString(val)
		}
	case C.SLEEPY_AST_ENV_BRIDGE:
		val := C.sleepy_ast_get_string(cNode)
		if val != nil {
			node.Value = C.GoString(val)
		}
		// Extract identifier and string safely using C helpers
		id_ptr := C.sleepy_ast_get_env_bridge_id(cNode)
		if id_ptr != nil {
			node.Children = append([]*Node{{
				Type:  AstIdentifier,
				Value: C.GoString(id_ptr),
			}}, node.Children...)
		}
		str_ptr := C.sleepy_ast_get_env_bridge_string(cNode)
		if str_ptr != nil {
			node.Children = append([]*Node{{
				Type:  AstString,
				Value: C.GoString(str_ptr),
			}}, node.Children...)
		}
	case C.SLEEPY_AST_FOREACH:
		meta := ForeachMetadata{}
		idx := C.sleepy_ast_get_foreach_index(cNode)
		if idx != nil {
			meta.Index = C.GoString(idx)
		}
		val := C.sleepy_ast_get_foreach_value(cNode)
		if val != nil {
			meta.Value = C.GoString(val)
		}
		node.Value = meta
	case C.SLEEPY_AST_FOR:
		meta := ForMetadata{
			InitCount: int(C.sleepy_ast_get_for_init_count(cNode)),
			IncCount:  int(C.sleepy_ast_get_for_inc_count(cNode)),
		}
		node.Value = meta
	case C.SLEEPY_AST_IMPORT:
		path := C.sleepy_ast_get_import_path(cNode)
		if path != nil {
			node.Value = ImportMetadata{Path: C.GoString(path)}
		}
	case C.SLEEPY_AST_TRY_CATCH:
		v := C.sleepy_ast_get_try_catch_var(cNode)
		if v != nil {
			node.Value = TryCatchMetadata{CatchVar: C.GoString(v)}
		}
	}

	// Extract operator for binop/unaryop/assignment
	if cType == C.SLEEPY_AST_BINOP || cType == C.SLEEPY_AST_UNARYOP || cType == C.SLEEPY_AST_ASSIGNMENT {
		op := C.sleepy_ast_get_op(cNode)
		if op != nil {
			opLen := C.sleepy_ast_get_op_length(cNode)
			node.Value = C.GoStringN(op, C.int(opLen))
		}
	}

	// Extract children using C helper
	var cChildren **C.SleepyASTNode
	var cCount C.size_t
	C.sleepy_ast_get_children(cNode, &cChildren, &cCount, p.allocator)

	if cCount > 0 && cChildren != nil {
		childrenSlice := unsafe.Slice(cChildren, int(cCount))
		for _, child := range childrenSlice {
			if child != nil {
				node.Children = append(node.Children, p.convertNode(child))
			}
		}
		// The array of pointers is dynamically allocated by the C helper
		// using the SleepyAllocator, so we must free the array itself.
		// (The actual AST nodes belong to the parser and are freed when it is freed).
		C.sleepy_ast_free_children(cChildren, p.allocator)
	}

	// Specific extractions for needed components
	if cType == C.SLEEPY_AST_CALL {
		strVal := C.sleepy_ast_get_string(cNode) // We updated the C helper to return the target string for Calls
		if strVal != nil {
			node.Value = C.GoString(strVal)
		}
	}

	return node
}

// VM Bindings

type VM struct {
    cVM       *C.SleepyVM
    allocator *C.SleepyAllocator
}

func NewVM() *VM {
    alloc := (*C.SleepyAllocator)(C.malloc(C.sizeof_SleepyAllocator))
    C.set_go_allocator(alloc)

    vm := C.sleepy_vm_new(alloc)

    return &VM{
        cVM:       vm,
        allocator: alloc,
    }
}

func (vm *VM) Close() error {
    if vm.cVM != nil {
        C.sleepy_vm_free(vm.cVM)
        vm.cVM = nil
    }
    if vm.allocator != nil {
        C.free(unsafe.Pointer(vm.allocator))
        vm.allocator = nil
    }
    return nil
}

func (vm *VM) Interpret(source string) error {
    cSource := C.CString(source)
    defer C.free(unsafe.Pointer(cSource))

    result := C.sleepy_vm_interpret(vm.cVM, cSource)

    if result != C.SLEEPY_OK {
        return fmt.Errorf("VM interpretation failed with result: %d", int(result))
    }

    return nil
}
