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

// Format serializes the AST node back into Sleepy source code.
func (n *Node) Format() string {
	if n == nil {
		return ""
	}

	// We'll use a temporary allocator for formatting
	alloc := (*C.SleepyAllocator)(C.malloc(C.sizeof_SleepyAllocator))
	C.set_go_allocator(alloc)
	defer C.free(unsafe.Pointer(alloc))

	// We need to reconstruct a C AST node for the unparser
	// Since our Go Node is a mirror, this is complex if we want to support 
	// round-tripping modified Go nodes back to C.
	// For now, let's implement a Go-native Formatter to make it easier to 
	// manipulate the AST in Go and still print it.
	return n.formatNative()
}

func (n *Node) formatNative() string {
	if n == nil {
		return ""
	}
	switch n.Type {
	case AstScript, AstBlock:
		res := ""
		if n.Type == AstBlock {
			res += "{\n"
		}
		for i, child := range n.Children {
			if n.Type == AstBlock {
				res += "    "
			}
			res += child.formatNative()
			if child.Type != AstEnvBridge && child.Type != AstIf && child.Type != AstWhile && child.Type != AstFor && child.Type != AstForeach {
				res += ";"
			}
			if i < len(n.Children)-1 || n.Type == AstBlock {
				res += "\n"
			}
		}
		if n.Type == AstBlock {
			res += "}"
		}
		return res
	case AstBoolean:
		if val, ok := n.Value.(bool); ok {
			if val {
				return "$true"
			}
			return "$false"
		}
		return "/* invalid bool */"
	case AstLong:
		if val, ok := n.Value.(int64); ok {
			return fmt.Sprintf("%dL", val)
		}
		return "/* invalid long */"
	case AstNumber:
		if val, ok := n.Value.(float64); ok {
			return fmt.Sprintf("%g", val)
		}
		return "/* invalid number */"
	case AstString, AstLiteral:
		if val, ok := n.Value.(string); ok {
			return val
		}
		return "/* invalid string */"
	case AstScalar:
		if val, ok := n.Value.(string); ok {
			return "$" + val
		}
		return "/* invalid scalar */"
	case AstIdentifier, AstArray, AstHashtable, AstClassLiteral:
		if val, ok := n.Value.(string); ok {
			return val
		}
		return "/* nil ID */"
	case AstCall:
		target := "/* missing call target */"
		if val, ok := n.Value.(string); ok {
			target = val
		}
		res := target + "("
		for i, child := range n.Children {
			res += child.formatNative()
			if i < len(n.Children)-1 {
				res += ", "
			}
		}
		res += ")"
		return res
	case AstArg:
		if len(n.Children) > 0 {
			return n.Children[0].formatNative()
		}
		return ""
	case AstAssignment:
		if len(n.Children) == 2 {
			return n.Children[0].formatNative() + " = " + n.Children[1].formatNative()
		}
		return "/* invalid assign */"
	case AstEnvBridge:
		res := "/* invalid env bridge */"
		if val, ok := n.Value.(string); ok {
			res = val
		}
		for _, child := range n.Children {
			res += " " + child.formatNative()
		}
		return res
	case AstIf:
		res := "if ("
		if len(n.Children) > 0 {
			res += n.Children[0].formatNative()
		}
		res += ") "
		if len(n.Children) > 1 {
			res += n.Children[1].formatNative()
		}
		if len(n.Children) > 2 {
			res += " else " + n.Children[2].formatNative()
		}
		return res
	default:
		return fmt.Sprintf("/* format not implemented for type %d */", n.Type)
	}
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

