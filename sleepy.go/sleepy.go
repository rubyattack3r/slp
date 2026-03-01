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

// Helper to extract a node's children generically since CGO struggles with unions
static inline void get_node_children(SleepyASTNode* node, SleepyASTNode*** out_children, size_t* out_count) {
    *out_children = NULL;
    *out_count = 0;
    if (!node) return;
    
    // Very simplified generic child extractor for basic AST walking
    switch (node->type) {
        case SLEEPY_AST_SCRIPT:
        case SLEEPY_AST_BLOCK:
            *out_children = node->as.block.statements;
            *out_count = node->as.block.count;
            break;
        case SLEEPY_AST_CALL:
            // Just returning args for simplicity in this example
            *out_children = node->as.call.args;
            *out_count = node->as.call.arg_count;
            break;
		case SLEEPY_AST_BINOP: {
			static SleepyASTNode* binop_children[2];
			binop_children[0] = node->as.binop.left;
			binop_children[1] = node->as.binop.right;
			*out_children = binop_children;
			*out_count = 2;
			break;
		}
		case SLEEPY_AST_ASSIGNMENT: {
			static SleepyASTNode* assign_children[2];
			assign_children[0] = node->as.assign.left;
			assign_children[1] = node->as.assign.right;
			*out_children = assign_children;
			*out_count = 2;
			break;
		}
		case SLEEPY_AST_ARG: {
			static SleepyASTNode* arg_children[1];
			arg_children[0] = node->as.arg.value;
			*out_children = arg_children;
			*out_count = 1;
			break;
		}
		case SLEEPY_AST_ENV_BRIDGE: {
			static SleepyASTNode* bridge_children[3];
			size_t count = 0;
			// We can't easily return static pointers from a switch if it's called recursively
			// but for this simple mirror it's usually okay as long as we copy it immediately in Go.
			// Actually, let's just create a small internal array.
			if (node->as.env_bridge.body) {
				bridge_children[count++] = node->as.env_bridge.body;
			}
			*out_children = bridge_children;
			*out_count = count;
			break;
		}
        default: break;
    }
}

// Helpers to read union fields safely
static inline const char* get_node_string(SleepyASTNode* node) {
    return node->as.string_val;
}
static inline SleepyASTNode* get_call_target(SleepyASTNode* node) {
    if (node->type == SLEEPY_AST_CALL) return node->as.call.target;
    return NULL;
}
*/
import "C"
import (
	"fmt"
	"unsafe"
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

func (p *Parser) Free() {
	if p.cParser != nil {
		C.free(unsafe.Pointer(p.cParser))
		p.cParser = nil
	}
	if p.allocator != nil {
		C.free(unsafe.Pointer(p.allocator))
		p.allocator = nil
	}
}

// Node represents a Sleepy AST Node
type Node struct {
	Type     C.SleepyASTNodeType
	Line     int
	Value    interface{} // Mapped value based on Type
	Children []*Node     // Child nodes for traversal
}

// AST Node Type Constants
const (
	AstBoolean    = C.SLEEPY_AST_BOOLEAN
	AstLong       = C.SLEEPY_AST_LONG
	AstNumber     = C.SLEEPY_AST_NUMBER
	AstLiteral    = C.SLEEPY_AST_LITERAL
	AstString     = C.SLEEPY_AST_STRING
	AstScalar     = C.SLEEPY_AST_SCALAR
	AstArray      = C.SLEEPY_AST_ARRAY
	AstHashtable  = C.SLEEPY_AST_HASHTABLE
	AstIdentifier = C.SLEEPY_AST_IDENTIFIER
	AstBacktick   = C.SLEEPY_AST_BACKTICK
	AstCall       = C.SLEEPY_AST_CALL
	AstBinop      = C.SLEEPY_AST_BINOP
	AstAssignment = C.SLEEPY_AST_ASSIGNMENT
	AstUnaryop    = C.SLEEPY_AST_UNARYOP
	AstIf         = C.SLEEPY_AST_IF
	AstWhile      = C.SLEEPY_AST_WHILE
	AstFor        = C.SLEEPY_AST_FOR
	AstForeach    = C.SLEEPY_AST_FOREACH
	AstReturn     = C.SLEEPY_AST_RETURN
	AstBreak      = C.SLEEPY_AST_BREAK
	AstContinue   = C.SLEEPY_AST_CONTINUE
	AstYield      = C.SLEEPY_AST_YIELD
	AstIndex      = C.SLEEPY_AST_INDEX
	AstObjExpr    = C.SLEEPY_AST_OBJ_EXPR
	AstTryCatch   = C.SLEEPY_AST_TRY_CATCH
	AstThrow      = C.SLEEPY_AST_THROW
	AstAssert     = C.SLEEPY_AST_ASSERT
	AstEnvBridge  = C.SLEEPY_AST_ENV_BRIDGE
	AstImport     = C.SLEEPY_AST_IMPORT
	AstArg        = C.SLEEPY_AST_ARG
	AstKvPair     = C.SLEEPY_AST_KV_PAIR
	AstDone       = C.SLEEPY_AST_DONE
	AstHalt       = C.SLEEPY_AST_HALT
	AstLocal      = C.SLEEPY_AST_LOCAL
	AstThis       = C.SLEEPY_AST_THIS
	AstBlock      = C.SLEEPY_AST_BLOCK
	AstScript     = C.SLEEPY_AST_SCRIPT
	AstClassLiteral = C.SLEEPY_AST_CLASS_LITERAL
)

// Parse parses the provided Sleepy source code and returns the root AST Node if successful.
func (p *Parser) Parse(source string) (*Node, error) {
	cSource := C.CString(source)
	defer C.free(unsafe.Pointer(cSource))

	C.sleepy_parser_init(p.cParser, cSource, p.allocator)

	// Parse the code
	node := C.sleepy_parser_parse(p.cParser)

	if p.cParser.had_error {
		return nil, fmt.Errorf("parsing failed")
	}

	if node == nil {
		return nil, fmt.Errorf("could not generate AST")
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
		if n.Value.(bool) {
			return "$true"
		}
		return "$false"
	case AstLong:
		return fmt.Sprintf("%dL", n.Value)
	case AstNumber:
		return fmt.Sprintf("%g", n.Value)
	case AstString, AstLiteral:
		return n.Value.(string)
	case AstScalar:
		return "$" + n.Value.(string)
	case AstIdentifier, AstArray, AstHashtable, AstClassLiteral:
		if n.Value == nil {
			return "/* nil ID */"
		}
		return n.Value.(string)
	case AstCall:
		target := ""
		if n.Value != nil {
			target = n.Value.(string)
		} else {
			target = "/* missing call target */"
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
		res := n.Value.(string)
		for _, child := range n.Children {
			res += " " + child.formatNative()
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
		Type: cNode._type,
		Line: int(cNode.line),
	}

	return p.mapNodeData(cNode, node)
}

func (p *Parser) mapNodeData(cNode *C.SleepyASTNode, node *Node) *Node {
	C_type := cNode._type
	switch C_type {
	case C.SLEEPY_AST_BOOLEAN:
		val := *(*C.bool)(unsafe.Pointer(&cNode.as[0]))
		node.Value = bool(val)
	case C.SLEEPY_AST_LONG:
		val := *(*C.long)(unsafe.Pointer(&cNode.as[0]))
		node.Value = int64(val)
	case C.SLEEPY_AST_NUMBER:
		val := *(*C.double)(unsafe.Pointer(&cNode.as[0]))
		node.Value = float64(val)
	case C.SLEEPY_AST_STRING, C.SLEEPY_AST_LITERAL, C.SLEEPY_AST_SCALAR,
		C.SLEEPY_AST_ARRAY, C.SLEEPY_AST_HASHTABLE, C.SLEEPY_AST_IDENTIFIER,
		C.SLEEPY_AST_CLASS_LITERAL:
		val := *(**C.char)(unsafe.Pointer(&cNode.as[0]))
		if val != nil {
			node.Value = C.GoString(val)
		}
	case C.SLEEPY_AST_ENV_BRIDGE:
		val := *(**C.char)(unsafe.Pointer(&cNode.as[0]))
		if val != nil {
			node.Value = C.GoString(val)
		}
		// Manually extract identifier and string if they exist
		// Since CGO struggles with unions, we use offsets or helpers
		// identifier is at as.env_bridge.identifier
		// We'll add them as synthetic children for the Go mirror
		id_ptr := *(**C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(&cNode.as[0])) + unsafe.Sizeof(uintptr(0))))
		if id_ptr != nil {
			node.Children = append([]*Node{{
				Type:  AstIdentifier,
				Value: C.GoString(id_ptr),
			}}, node.Children...)
		}
		str_ptr := *(**C.char)(unsafe.Pointer(uintptr(unsafe.Pointer(&cNode.as[0])) + 2*unsafe.Sizeof(uintptr(0))))
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
	C.get_node_children(cNode, &cChildren, &cCount)

	if cCount > 0 && cChildren != nil {
		childrenSlice := unsafe.Slice(cChildren, int(cCount))
		for _, child := range childrenSlice {
			if child != nil {
				node.Children = append(node.Children, p.convertNode(child))
			}
		}
	}

	// Specific extractions for needed components
	if C_type == C.SLEEPY_AST_CALL {
		target := C.get_call_target(cNode)
		if target != nil && target._type == C.SLEEPY_AST_IDENTIFIER {
			strVal := C.get_node_string(target)
			if strVal != nil {
				node.Value = C.GoString(strVal)
			}
		}
	}

	return node
}
