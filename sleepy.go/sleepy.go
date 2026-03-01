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
	AstLiteral = C.SLEEPY_AST_LITERAL
	AstString  = C.SLEEPY_AST_STRING
	AstBinop   = C.SLEEPY_AST_BINOP
	AstCall    = C.SLEEPY_AST_CALL
	AstArg     = C.SLEEPY_AST_ARG
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

// Helper to convert C.SleepyASTNode to Go Node
func (p *Parser) convertNode(cNode *C.SleepyASTNode) *Node {
	if cNode == nil {
		return nil
	}

	node := &Node{
		Type: cNode._type,
		Line: int(cNode.line),
	}
	
	// Add AST conversion logic here based on node._type
	return p.mapNodeData(cNode, node)
}

func (p *Parser) mapNodeData(cNode *C.SleepyASTNode, node *Node) *Node {
	// Since Go can't safely access C unions without knowing the active member
	// we map them carefully based on the Type.
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
				// We overload Value for CALL to be the target string name if available
				node.Value = C.GoString(strVal)
			}
		}
	}

	return node
}
