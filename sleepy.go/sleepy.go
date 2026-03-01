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
	Value    interface{} // Mapped value based on Type
	Children []*Node     // Child nodes for traversal
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
	AstIndex        NodeType = C.SLEEPY_AST_INDEX
	AstObjExpr      NodeType = C.SLEEPY_AST_OBJ_EXPR
	AstTryCatch     NodeType = C.SLEEPY_AST_TRY_CATCH
	AstThrow        NodeType = C.SLEEPY_AST_THROW
	AstAssert       NodeType = C.SLEEPY_AST_ASSERT
	AstEnvBridge    NodeType = C.SLEEPY_AST_ENV_BRIDGE
	AstImport       NodeType = C.SLEEPY_AST_IMPORT
	AstArg          NodeType = C.SLEEPY_AST_ARG
	AstKvPair       NodeType = C.SLEEPY_AST_KV_PAIR
	AstDone         NodeType = C.SLEEPY_AST_DONE
	AstHalt         NodeType = C.SLEEPY_AST_HALT
	AstLocal        NodeType = C.SLEEPY_AST_LOCAL
	AstThis         NodeType = C.SLEEPY_AST_THIS
	AstBlock        NodeType = C.SLEEPY_AST_BLOCK
	AstScript       NodeType = C.SLEEPY_AST_SCRIPT
	AstClassLiteral NodeType = C.SLEEPY_AST_CLASS_LITERAL
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
	case AstArg:
		return "AstArg"
	case AstKvPair:
		return "AstKvPair"
	case AstDone:
		return "AstDone"
	case AstHalt:
		return "AstHalt"
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
		return nil, ErrParsingFailed
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
		cChildren := make([]*C.SleepyASTNode, len(node.Children))
		for i, child := range node.Children {
			cChildren[i] = p.buildCNode(child)
		}
		// CGo trick to get pointer to array elements
		return (**C.SleepyASTNode)(unsafe.Pointer(&cChildren[0])), C.size_t(len(node.Children))
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
			C.sleepy_ast_set_binop(cNode, cLeft, cRight)
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
		if len(node.Children) > 0 {
			cVal := p.buildCNode(node.Children[0])
			C.sleepy_ast_set_arg(cNode, cVal)
		}

	case AstAssignment:
		if len(node.Children) == 2 {
			cTarget := p.buildCNode(node.Children[0])
			cVal := p.buildCNode(node.Children[1])
			C.sleepy_ast_set_assignment(cNode, cTarget, cVal)
		}

	case AstEnvBridge:
		var cId, cStr *C.char
		var cChildrenArr **C.SleepyASTNode
		var cCount C.size_t

		if len(node.Children) >= 2 && node.Children[0].Type == AstIdentifier && node.Children[1].Type == AstString {
			// Extract our artificial children back into the original ID strings
			if val, ok := node.Children[0].Value.(string); ok {
				cId = C.CString(val)
				defer C.free(unsafe.Pointer(cId))
			}
			if val, ok := node.Children[1].Value.(string); ok {
				cStr = C.CString(val)
				defer C.free(unsafe.Pointer(cStr))
			}

			// Any remaining children are actual bridge arguments
			if len(node.Children) > 2 {
				origChildren := node.Children
				node.Children = node.Children[2:]
				cChildrenArr, cCount = buildChildren()
				node.Children = origChildren // restore
			}
			C.sleepy_ast_set_env_bridge(cNode, cId, cStr, cChildrenArr, cCount, p.allocator)
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
		C.SLEEPY_AST_CLASS_LITERAL:
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

