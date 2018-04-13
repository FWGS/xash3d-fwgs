//=========== (C) Copyright 1999 Valve, L.L.C. All rights reserved. ===========
//
// The copyright to the contents herein is the property of Valve, L.L.C.
// The contents may be used and/or copied only with the written permission of
// Valve, L.L.C., or in accordance with the terms and conditions stipulated in
// the agreement/contract under which the contents have been supplied.
//
// Purpose: 
//
// $Header: $
// $NoKeywords: $
//=============================================================================

#ifndef UTLRBTREE_H
#define UTLRBTREE_H

#include "port.h"
#include "utlmemory.h"
//-----------------------------------------------------------------------------
// Tool to generate a default compare function for any type that implements
// operator<, including all simple types
//-----------------------------------------------------------------------------

template <typename T >
class CDefOps
{
public:
	static bool LessFunc( const T &lhs, const T &rhs ) { return ( lhs < rhs ); }
};

#define DefLessFunc( type ) CDefOps<type>::LessFunc

//-------------------------------------

inline bool StringLessThan( const char * const &lhs, const char * const &rhs) { return ( strcmp( lhs, rhs) < 0 );  }
inline bool CaselessStringLessThan( const char * const &lhs, const char * const &rhs ) { return ( strcasecmp( lhs, rhs) < 0 ); }

//-------------------------------------
// inline these two templates to stop multiple definitions of the same code
template <> inline bool CDefOps<const char *>::LessFunc( const char * const &lhs, const char * const &rhs ) { return StringLessThan( lhs, rhs ); }
template <> inline bool CDefOps<char *>::LessFunc( char * const &lhs, char * const &rhs )	{ return StringLessThan( lhs, rhs ); }

//-------------------------------------

template <typename RBTREE_T>
void SetDefLessFunc( RBTREE_T &RBTree )
{
#ifdef _WIN32
	RBTree.SetLessFunc( DefLessFunc( RBTREE_T::KeyType_t ) );
#elif _LINUX
	RBTree.SetLessFunc( DefLessFunc( typename RBTREE_T::KeyType_t ) );
#endif
}

//-----------------------------------------------------------------------------
// A red-black binary search tree
//-----------------------------------------------------------------------------

template <class T, class I = unsigned short> 
class CUtlRBTree
{
public:
	// Less func typedef
	// Returns true if the first parameter is "less" than the second
	typedef bool (*LessFunc_t)( T const &, T const & );
	
	typedef T KeyType_t;
	typedef T ElemType_t;
	typedef I IndexType_t;

	// constructor, destructor
	// Left at growSize = 0, the memory will first allocate 1 element and double in size
	// at each increment.
	// LessFunc_t is required, but may be set after the constructor using SetLessFunc() below
	CUtlRBTree( int growSize = 0, int initSize = 0, LessFunc_t lessfunc = 0 );
	~CUtlRBTree( );
	
	// gets particular elements
	T&			Element( I i );
	T const		&Element( I i ) const;
	T&			operator[]( I i );
	T const		&operator[]( I i ) const;
	
	// Gets the root
	I  Root() const;
	
	// Num elements
	unsigned int Count() const;
	
	// Max "size" of the vector
	I  MaxElement() const;
	
	// Gets the children                               
	I  Parent( I i ) const;
	I  LeftChild( I i ) const;
	I  RightChild( I i ) const;
	
	// Tests if a node is a left or right child
	bool  IsLeftChild( I i ) const;
	bool  IsRightChild( I i ) const;
	
	// Tests if root or leaf
	bool  IsRoot( I i ) const;
	bool  IsLeaf( I i ) const;
	
	// Checks if a node is valid and in the tree
	bool  IsValidIndex( I i ) const;
	
	// Checks if the tree as a whole is valid
	bool  IsValid() const;
	
	// Invalid index
	static I InvalidIndex();
	
	// returns the tree depth (not a very fast operation)
	int   Depth( I node ) const;
	int   Depth() const;
	
	// Sets the less func
	void SetLessFunc( LessFunc_t func );
	
	// Allocation method
	I  NewNode();

	// Insert method (inserts in order)
	I  Insert( T const &insert );
	void Insert( const T *pArray, int nItems );
	
	// Find method
	I  Find( T const &search ) const;
	
	// Remove methods
	void     RemoveAt( I i );
	bool     Remove( T const &remove );
	void     RemoveAll( );
			
	// Allocation, deletion
	void  FreeNode( I i );
	
	// Iteration
	I  FirstInorder() const;
	I  NextInorder( I i ) const;
	I  PrevInorder( I i ) const;
	I  LastInorder() const;
	
	I  FirstPreorder() const;
	I  NextPreorder( I i ) const;
	I  PrevPreorder( I i ) const;
	I  LastPreorder( ) const;
	
	I  FirstPostorder() const;
	I  NextPostorder( I i ) const;

	// If you change the search key, this can be used to reinsert the 
	// element into the tree.
	void	Reinsert( I elem );

protected:
	enum NodeColor_t
	{
		RED = 0,
		BLACK
	};
	
	struct Links_t
	{
		I  m_Left;
		I  m_Right;
		I  m_Parent;
		I  m_Tag;
	};
	
	struct Node_t : public Links_t
	{
		T  m_Data;
	};

	// Sets the children
	void  SetParent( I i, I parent );
	void  SetLeftChild( I i, I child  );
	void  SetRightChild( I i, I child  );
	void  LinkToParent( I i, I parent, bool isLeft );

	// Gets at the links
	Links_t const	&Links( I i ) const;
	Links_t			&Links( I i );      
	
	// Checks if a link is red or black
	bool IsRed( I i ) const;
	bool IsBlack( I i ) const;
	
	// Sets/gets node color
	NodeColor_t Color( I i ) const;
	void        SetColor( I i, NodeColor_t c );
	
	// operations required to preserve tree balance
	void RotateLeft(I i);
	void RotateRight(I i);
	void InsertRebalance(I i);
	void RemoveRebalance(I i);
	
	// Insertion, removal
	I  InsertAt( I parent, bool leftchild );
	
	// copy constructors not allowed
	CUtlRBTree( CUtlRBTree<T, I> const &tree );
	
	// Inserts a node into the tree, doesn't copy the data in.
	void FindInsertionPosition( T const &insert, I &parent, bool &leftchild );

	// Remove and add back an element in the tree.
	void	Unlink( I elem );
	void	Link( I elem );

	// Used for sorting.
	LessFunc_t m_LessFunc;
	
	CUtlMemory<Node_t> m_Elements;
	I m_Root;
	I m_NumElements;
	I m_FirstFree;
	I m_TotalElements;

	Node_t*   m_pElements;
	
	void ResetDbgInfo()
	{
		m_pElements = (Node_t*)m_Elements.Base();
	}				    
};


//-----------------------------------------------------------------------------
// constructor, destructor
//-----------------------------------------------------------------------------

template <class T, class I>
CUtlRBTree<T, I>::CUtlRBTree( int growSize, int initSize, LessFunc_t lessfunc ) : 
	m_Elements( growSize, initSize ), 
	m_LessFunc( lessfunc ),
	m_Root( InvalidIndex() ), 
	m_NumElements( 0 ), m_TotalElements( 0 ),  
	m_FirstFree( InvalidIndex() )
{
	ResetDbgInfo();
}

template <class T, class I> 
CUtlRBTree<T, I>::~CUtlRBTree()
{
}

//-----------------------------------------------------------------------------
// gets particular elements
//-----------------------------------------------------------------------------

template <class T, class I>
inline T &CUtlRBTree<T, I>::Element( I i )        
{ 
	return m_Elements[i].m_Data; 
}

template <class T, class I>
inline T const &CUtlRBTree<T, I>::Element( I i ) const  
{ 
	return m_Elements[i].m_Data; 
}

template <class T, class I>
inline T &CUtlRBTree<T, I>::operator[]( I i )        
{ 
	return Element(i); 
}

template <class T, class I>
inline T const &CUtlRBTree<T, I>::operator[]( I i ) const  
{ 
	return Element(i); 
}

//-----------------------------------------------------------------------------
//
// various accessors
//
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Gets the root
//-----------------------------------------------------------------------------

template <class T, class I>
inline	I  CUtlRBTree<T, I>::Root() const             
{ 
	return m_Root; 
}
	
//-----------------------------------------------------------------------------
// Num elements
//-----------------------------------------------------------------------------

template <class T, class I>
inline	unsigned int CUtlRBTree<T, I>::Count() const          
{ 
	return (unsigned int)m_NumElements; 
}
	
//-----------------------------------------------------------------------------
// Max "size" of the vector
//-----------------------------------------------------------------------------

template <class T, class I>
inline	I  CUtlRBTree<T, I>::MaxElement() const       
{ 
	return (I)m_TotalElements; 
}
	

//-----------------------------------------------------------------------------
// Gets the children                               
//-----------------------------------------------------------------------------

template <class T, class I>
inline	I CUtlRBTree<T, I>::Parent( I i ) const      
{ 
	return Links(i).m_Parent; 
}

template <class T, class I>
inline	I CUtlRBTree<T, I>::LeftChild( I i ) const   
{ 
	return Links(i).m_Left; 
}

template <class T, class I>
inline	I CUtlRBTree<T, I>::RightChild( I i ) const  
{ 
	return Links(i).m_Right; 
}
	
//-----------------------------------------------------------------------------
// Tests if a node is a left or right child
//-----------------------------------------------------------------------------

template <class T, class I>
inline	bool CUtlRBTree<T, I>::IsLeftChild( I i ) const 
{ 
	return LeftChild(Parent(i)) == i; 
}

template <class T, class I>
inline	bool CUtlRBTree<T, I>::IsRightChild( I i ) const
{ 
	return RightChild(Parent(i)) == i; 
}
	

//-----------------------------------------------------------------------------
// Tests if root or leaf
//-----------------------------------------------------------------------------

template <class T, class I>
inline	bool CUtlRBTree<T, I>::IsRoot( I i ) const     
{ 
	return i == m_Root; 
}

template <class T, class I>
inline	bool CUtlRBTree<T, I>::IsLeaf( I i ) const     
{ 
	return (LeftChild(i) == InvalidIndex()) && (RightChild(i) == InvalidIndex()); 
}
	

//-----------------------------------------------------------------------------
// Checks if a node is valid and in the tree
//-----------------------------------------------------------------------------

template <class T, class I>
inline	bool CUtlRBTree<T, I>::IsValidIndex( I i ) const 
{ 
	return LeftChild(i) != i; 
}
	
	
//-----------------------------------------------------------------------------
// Invalid index
//-----------------------------------------------------------------------------

template <class T, class I>
I CUtlRBTree<T, I>::InvalidIndex()         
{ 
	return (I)~0; 
}

	
//-----------------------------------------------------------------------------
// returns the tree depth (not a very fast operation)
//-----------------------------------------------------------------------------

template <class T, class I>
inline int CUtlRBTree<T, I>::Depth() const           
{ 
	return Depth(Root()); 
}

//-----------------------------------------------------------------------------
// Sets the children
//-----------------------------------------------------------------------------

template <class T, class I>
inline void  CUtlRBTree<T, I>::SetParent( I i, I parent )       
{ 
	Links(i).m_Parent = parent; 
}

template <class T, class I>
inline void  CUtlRBTree<T, I>::SetLeftChild( I i, I child  )    
{ 
	Links(i).m_Left = child; 
}

template <class T, class I>
inline void  CUtlRBTree<T, I>::SetRightChild( I i, I child  )   
{ 
	Links(i).m_Right = child; 
}

//-----------------------------------------------------------------------------
// Gets at the links
//-----------------------------------------------------------------------------
static const int s_Sentinel[4] = {-1, -1, -1, 1};
template <class T, class I>
inline typename CUtlRBTree<T, I>::Links_t const &CUtlRBTree<T, I>::Links( I i ) const
{
	// Sentinel node, makes life easier
	
	return (i != InvalidIndex()) ? *(Links_t*)&m_Elements[i] :
								 *(Links_t*)&s_Sentinel; 
}

template <class T, class I>
inline typename CUtlRBTree<T, I>::Links_t &CUtlRBTree<T, I>::Links( I i )       
{ 
	Assert(i != InvalidIndex()); 
	return *(Links_t *)&m_Elements[i];
}

//-----------------------------------------------------------------------------
// Checks if a link is red or black
//-----------------------------------------------------------------------------

template <class T, class I>
inline bool CUtlRBTree<T, I>::IsRed( I i ) const                
{ 
	return (Links(i).m_Tag == RED); 
}

template <class T, class I>
inline bool CUtlRBTree<T, I>::IsBlack( I i ) const             
{ 
	return (Links(i).m_Tag == BLACK); 
}


//-----------------------------------------------------------------------------
// Sets/gets node color
//-----------------------------------------------------------------------------

template <class T, class I>
inline typename CUtlRBTree<T, I>::NodeColor_t  CUtlRBTree<T, I>::Color( I i ) const            
{ 
	return (NodeColor_t)Links(i).m_Tag; 
}

template <class T, class I>
inline void CUtlRBTree<T, I>::SetColor( I i, typename CUtlRBTree<T, I>::NodeColor_t c ) 
{ 
	Links(i).m_Tag = (I)c; 
}

//-----------------------------------------------------------------------------
// Allocates/ deallocates nodes
//-----------------------------------------------------------------------------

template <class T, class I>
I  CUtlRBTree<T, I>::NewNode()
{
	I newElem;
	
	// Nothing in the free list; add.
	if (m_FirstFree == InvalidIndex())
	{
		if (m_Elements.NumAllocated() == m_TotalElements)
			m_Elements.Grow();
		newElem = m_TotalElements++;
	}
	else
	{
		newElem = m_FirstFree;
		m_FirstFree = RightChild(m_FirstFree);
	}
	
#ifdef _DEBUG
	// reset links to invalid....
	Links_t &node = Links(newElem);
	node.m_Left = node.m_Right = node.m_Parent = InvalidIndex();
#endif
	
	Construct( &Element(newElem) );
	ResetDbgInfo();
	
	return newElem;
}

template <class T, class I>
void  CUtlRBTree<T, I>::FreeNode( I i )
{
	Assert( IsValidIndex(i) && (i != InvalidIndex()) );
	Destruct( &Element(i) );
	SetLeftChild( i, i ); // indicates it's in not in the tree
	SetRightChild( i, m_FirstFree );
	m_FirstFree = i;
}


//-----------------------------------------------------------------------------
// Rotates node i to the left
//-----------------------------------------------------------------------------

template <class T, class I>
void CUtlRBTree<T, I>::RotateLeft(I elem) 
{
	I rightchild = RightChild(elem);
	SetRightChild( elem, LeftChild(rightchild) );
	if (LeftChild(rightchild) != InvalidIndex())
		SetParent( LeftChild(rightchild), elem );
	
	if (rightchild != InvalidIndex())
		SetParent( rightchild, Parent(elem) );
	if (!IsRoot(elem))
	{
		if (IsLeftChild(elem))
			SetLeftChild( Parent(elem), rightchild );
		else
			SetRightChild( Parent(elem), rightchild );
	}
	else
		m_Root = rightchild;
	
	SetLeftChild( rightchild, elem );
	if (elem != InvalidIndex())
		SetParent( elem, rightchild );
}


//-----------------------------------------------------------------------------
// Rotates node i to the right
//-----------------------------------------------------------------------------

template <class T, class I>
void CUtlRBTree<T, I>::RotateRight(I elem) 
{
	I leftchild = LeftChild(elem);
	SetLeftChild( elem, RightChild(leftchild) );
	if (RightChild(leftchild) != InvalidIndex())
		SetParent( RightChild(leftchild), elem );
	
	if (leftchild != InvalidIndex())
		SetParent( leftchild, Parent(elem) );
	if (!IsRoot(elem))
	{
		if (IsRightChild(elem))
			SetRightChild( Parent(elem), leftchild );
		else
			SetLeftChild( Parent(elem), leftchild );
	}
	else
		m_Root = leftchild;
	
	SetRightChild( leftchild, elem );
	if (elem != InvalidIndex())
		SetParent( elem, leftchild );
}


//-----------------------------------------------------------------------------
// Rebalances the tree after an insertion
//-----------------------------------------------------------------------------

template <class T, class I>
void CUtlRBTree<T, I>::InsertRebalance(I elem) 
{
	while ( !IsRoot(elem) && (Color(Parent(elem)) == RED) )
	{
		I parent = Parent(elem);
		I grandparent = Parent(parent);
		
		/* we have a violation */
		if (IsLeftChild(parent))
		{
			I uncle = RightChild(grandparent);
			if (IsRed(uncle)) 
            {
				/* uncle is RED */
				SetColor(parent, BLACK);
				SetColor(uncle, BLACK);
				SetColor(grandparent, RED);
				elem = grandparent;
            } 
			else 
            {
				/* uncle is BLACK */
				if (IsRightChild(elem))
				{
					/* make x a left child, will change parent and grandparent */
					elem = parent;
					RotateLeft(elem);
					parent = Parent(elem);
					grandparent = Parent(parent);
				}
				/* recolor and rotate */
				SetColor(parent, BLACK);
				SetColor(grandparent, RED);
				RotateRight(grandparent);
            }
		} 
		else 
		{
			/* mirror image of above code */
			I uncle = LeftChild(grandparent);
			if (IsRed(uncle)) 
            {
				/* uncle is RED */
				SetColor(parent, BLACK);
				SetColor(uncle, BLACK);
				SetColor(grandparent, RED);
				elem = grandparent;
            } 
			else 
            {
				/* uncle is BLACK */
				if (IsLeftChild(elem))
				{
					/* make x a right child, will change parent and grandparent */
					elem = parent;
					RotateRight(parent);
					parent = Parent(elem);
					grandparent = Parent(parent);
				}
				/* recolor and rotate */
				SetColor(parent, BLACK);
				SetColor(grandparent, RED);
				RotateLeft(grandparent);
            }
		}
	}
	SetColor( m_Root, BLACK );
}


//-----------------------------------------------------------------------------
// Insert a node into the tree
//-----------------------------------------------------------------------------

template <class T, class I>
I CUtlRBTree<T, I>::InsertAt( I parent, bool leftchild )
{
	I i = NewNode();
	LinkToParent( i, parent, leftchild );
	++m_NumElements;
	return i;
}

template <class T, class I>
void CUtlRBTree<T, I>::LinkToParent( I i, I parent, bool isLeft )
{
	Links_t &elem = Links(i);
	elem.m_Parent = parent;
	elem.m_Left = elem.m_Right = InvalidIndex();
	elem.m_Tag = RED;
	
	/* insert node in tree */
	if (parent != InvalidIndex()) 
	{
		if (isLeft)
			Links(parent).m_Left = i;
		else
			Links(parent).m_Right = i;
	} 
	else 
	{
		m_Root = i;
	}
	
	InsertRebalance(i);	

	Assert(IsValid());
}

//-----------------------------------------------------------------------------
// Rebalance the tree after a deletion
//-----------------------------------------------------------------------------

template <class T, class I>
void CUtlRBTree<T, I>::RemoveRebalance(I elem) 
{
	while (elem != m_Root && IsBlack(elem)) 
	{
		I parent = Parent(elem);
		
		// If elem is the left child of the parent
		if (elem == LeftChild(parent)) 
		{
			// Get our sibling
			I sibling = RightChild(parent);
			if (IsRed(sibling)) 
            {
				SetColor(sibling, BLACK);
				SetColor(parent, RED);
				RotateLeft(parent);
				
				// We may have a new parent now
				parent = Parent(elem);
				sibling = RightChild(parent);
            }
			if ( (IsBlack(LeftChild(sibling))) && (IsBlack(RightChild(sibling))) ) 
            {
				if (sibling != InvalidIndex())
					SetColor(sibling, RED);
				elem = parent;
            }
			else
            {
				if (IsBlack(RightChild(sibling)))
				{
					SetColor(LeftChild(sibling), BLACK);
					SetColor(sibling, RED);
					RotateRight(sibling);
					
					// rotation may have changed this
					parent = Parent(elem);
					sibling = RightChild(parent);
				}
				SetColor( sibling, Color(parent) );
				SetColor( parent, BLACK );
				SetColor( RightChild(sibling), BLACK );
				RotateLeft( parent );
				elem = m_Root;
            }
		}
		else 
		{
			// Elem is the right child of the parent
			I sibling = LeftChild(parent);
			if (IsRed(sibling)) 
            {
				SetColor(sibling, BLACK);
				SetColor(parent, RED);
				RotateRight(parent);
				
				// We may have a new parent now
				parent = Parent(elem);
				sibling = LeftChild(parent);
            }
			if ( (IsBlack(RightChild(sibling))) && (IsBlack(LeftChild(sibling))) )
            {
				if (sibling != InvalidIndex()) 
					SetColor( sibling, RED );
				elem = parent;
            } 
			else 
            {
				if (IsBlack(LeftChild(sibling)))
				{
					SetColor( RightChild(sibling), BLACK );
					SetColor( sibling, RED );
					RotateLeft( sibling );
					
					// rotation may have changed this
					parent = Parent(elem);
					sibling = LeftChild(parent);
				}
				SetColor( sibling, Color(parent) );
				SetColor( parent, BLACK );
				SetColor( LeftChild(sibling), BLACK );
				RotateRight( parent );
				elem = m_Root;
            }
		}
	}
	SetColor( elem, BLACK );
}

template <class T, class I>
void CUtlRBTree<T, I>::Unlink( I elem )
{
	if ( elem != InvalidIndex() )
	{
		I x, y;
		
		if ((LeftChild(elem) == InvalidIndex()) || 
			(RightChild(elem) == InvalidIndex()))
		{
			/* y has a NIL node as a child */
			y = elem;
		}
		else
		{
			/* find tree successor with a NIL node as a child */
			y = RightChild(elem);
			while (LeftChild(y) != InvalidIndex())
				y = LeftChild(y);
		}
		
		/* x is y's only child */
		if (LeftChild(y) != InvalidIndex())
			x = LeftChild(y);
		else
			x = RightChild(y);
		
		/* remove y from the parent chain */
		if (x != InvalidIndex())
			SetParent( x, Parent(y) );
		if (!IsRoot(y))
		{
			if (IsLeftChild(y))
				SetLeftChild( Parent(y), x );
			else
				SetRightChild( Parent(y), x );
		}
		else
			m_Root = x;
		
		// need to store this off now, we'll be resetting y's color
		NodeColor_t ycolor = Color(y);
		if (y != elem)
		{
			// Standard implementations copy the data around, we cannot here.
			// Hook in y to link to the same stuff elem used to.
			SetParent( y, Parent(elem) );
			SetRightChild( y, RightChild(elem) );
			SetLeftChild( y, LeftChild(elem) );
			
			if (!IsRoot(elem))
				if (IsLeftChild(elem))
					SetLeftChild( Parent(elem), y );
				else
					SetRightChild( Parent(elem), y );
				else
					m_Root = y;
				
				if (LeftChild(y) != InvalidIndex())
					SetParent( LeftChild(y), y );
				if (RightChild(y) != InvalidIndex())
					SetParent( RightChild(y), y );
				
				SetColor( y, Color(elem) );
		}
		
		if ((x != InvalidIndex()) && (ycolor == BLACK))
			RemoveRebalance(x);
	}
}

template <class T, class I>
void CUtlRBTree<T, I>::Link( I elem )
{
	if ( elem != InvalidIndex() )
	{
		I parent;
		bool leftchild;

		FindInsertionPosition( Element( elem ), parent, leftchild );

		LinkToParent( elem, parent, leftchild );
	}
}

//-----------------------------------------------------------------------------
// Delete a node from the tree
//-----------------------------------------------------------------------------

template <class T, class I>
void CUtlRBTree<T, I>::RemoveAt(I elem) 
{
	if ( elem != InvalidIndex() )
	{
		Unlink( elem );

		FreeNode(elem);
		--m_NumElements;
	}
}


//-----------------------------------------------------------------------------
// remove a node in the tree
//-----------------------------------------------------------------------------

template <class T, class I> bool CUtlRBTree<T, I>::Remove( T const &search )
{
	I node = Find( search );
	if (node != InvalidIndex())
	{
		RemoveAt(node);
		return true;
	}
	return false;
}


//-----------------------------------------------------------------------------
// Removes all nodes from the tree
//-----------------------------------------------------------------------------

template <class T, class I>
void CUtlRBTree<T, I>::RemoveAll()
{
	// Just iterate through the whole list and add to free list
	// much faster than doing all of the rebalancing
	// also, do it so the free list is pointing to stuff in order
	// to get better cache coherence when re-adding stuff to this tree.
	I prev = InvalidIndex();
	for (int i = (int)m_TotalElements; --i >= 0; )
	{
		I idx = (I)i;
		if (IsValidIndex(idx))
			Destruct( &Element(idx) );
		SetRightChild( idx, prev );
		SetLeftChild( idx, idx );
		prev = idx;
	}
	m_FirstFree = m_TotalElements ? (I)0 : InvalidIndex();
	m_Root = InvalidIndex();
	m_NumElements = 0;
}


//-----------------------------------------------------------------------------
// iteration
//-----------------------------------------------------------------------------

template <class T, class I>
I CUtlRBTree<T, I>::FirstInorder() const
{
	I i = m_Root;
	while (LeftChild(i) != InvalidIndex())
		i = LeftChild(i);
	return i;
}

template <class T, class I>
I CUtlRBTree<T, I>::NextInorder( I i ) const
{
	Assert(IsValidIndex(i));
	
	if (RightChild(i) != InvalidIndex())
	{
		i = RightChild(i);
		while (LeftChild(i) != InvalidIndex())
			i = LeftChild(i);
		return i;
	}
	
	I parent = Parent(i);
	while (IsRightChild(i))
	{
		i = parent;
		if (i == InvalidIndex()) break;
		parent = Parent(i);
	}
	return parent;
}

template <class T, class I>
I CUtlRBTree<T, I>::PrevInorder( I i ) const
{
	Assert(IsValidIndex(i));
	
	if (LeftChild(i) != InvalidIndex())
	{
		i = LeftChild(i);
		while (RightChild(i) != InvalidIndex())
			i = RightChild(i);
		return i;
	}
	
	I parent = Parent(i);
	while (IsLeftChild(i))
	{
		i = parent;
		if (i == InvalidIndex()) break;
		parent = Parent(i);
	}
	return parent;
}

template <class T, class I>
I CUtlRBTree<T, I>::LastInorder() const
{
	I i = m_Root;
	while (RightChild(i) != InvalidIndex())
		i = RightChild(i);
	return i;
}

template <class T, class I>
I CUtlRBTree<T, I>::FirstPreorder() const
{
	return m_Root;
}

template <class T, class I>
I CUtlRBTree<T, I>::NextPreorder( I i ) const
{
	if (LeftChild(i) != InvalidIndex())
		return LeftChild(i);
	
	if (RightChild(i) != InvalidIndex())
		return RightChild(i);
	
	I parent = Parent(i);
	while( parent != InvalidIndex())
	{
		if (IsLeftChild(i) && (RightChild(parent) != InvalidIndex()))
			return RightChild(parent);
		i = parent;
		parent = Parent(parent);
	}
	return InvalidIndex();
}

template <class T, class I>
I CUtlRBTree<T, I>::PrevPreorder( I i ) const
{
	Assert(0);  // not implemented yet
	return InvalidIndex();
}

template <class T, class I>
I CUtlRBTree<T, I>::LastPreorder() const
{
	I i = m_Root;
	while (1)
	{
		while (RightChild(i) != InvalidIndex())
			i = RightChild(i);
		
		if (LeftChild(i) != InvalidIndex())
			i = LeftChild(i);
		else
			break;
	}
	return i;
}

template <class T, class I>
I CUtlRBTree<T, I>::FirstPostorder() const
{
	I i = m_Root;
	while (!IsLeaf(i))
	{
		if (LeftChild(i))
			i = LeftChild(i);
		else
			i = RightChild(i);
	}
	return i;
}

template <class T, class I>
I CUtlRBTree<T, I>::NextPostorder( I i ) const
{
	I parent = Parent(i);
	if (parent == InvalidIndex())
		return InvalidIndex();
	
	if (IsRightChild(i))
		return parent;
	
	if (RightChild(parent) == InvalidIndex())
		return parent;
	
	i = RightChild(parent);
	while (!IsLeaf(i))
	{
		if (LeftChild(i))
			i = LeftChild(i);
		else
			i = RightChild(i);
	}
	return i;
}


template <class T, class I>
void CUtlRBTree<T, I>::Reinsert( I elem )
{
	Unlink( elem );
	Link( elem );
}


//-----------------------------------------------------------------------------
// returns the tree depth (not a very fast operation)
//-----------------------------------------------------------------------------
#ifdef max
#undef max
#endif
#define max(a,b) (a)>(b)?(a):(b)

template <class T, class I>
int CUtlRBTree<T, I>::Depth( I node ) const
{
	if (node == InvalidIndex())
		return 0;
	
	int depthright = Depth( RightChild(node) );
	int depthleft = Depth( LeftChild(node) );
	return max(depthright, depthleft) + 1;
}


//-----------------------------------------------------------------------------
// Makes sure the tree is valid after every operation
//-----------------------------------------------------------------------------

template <class T, class I>
bool CUtlRBTree<T, I>::IsValid() const
{
	if ( !Count() )
		return true;
	
	if (( Root() >= MaxElement()) || ( Parent( Root() ) != InvalidIndex() )) 
		goto InvalidTree;
	
#ifdef UTLTREE_PARANOID
	
	// First check to see that mNumEntries matches reality.
	// count items on the free list
	int numFree = 0;
	int curr = m_FirstFree;
	while (curr != InvalidIndex())
	{
		++numFree;
		curr = RightChild(curr);
		if ( (curr > MaxElement()) && (curr != InvalidIndex()) )
			goto InvalidTree;
	}
	if (MaxElement() - numFree != Count())
		goto InvalidTree;
	
	// iterate over all elements, looking for validity 
	// based on the self pointers
	int numFree2 = 0;
	for (curr = 0; curr < MaxElement(); ++curr)
	{
		if (!IsValidIndex(curr))
			++numFree2;
		else
		{
			int right = RightChild(curr);
			int left = LeftChild(curr);
			if ((right == left) && (right != InvalidIndex()) )
				goto InvalidTree;
			
			if (right != InvalidIndex())
            {
				if (!IsValidIndex(right)) 
					goto InvalidTree;
				if (Parent(right) != curr) 
					goto InvalidTree;
				if (IsRed(curr) && IsRed(right)) 
					goto InvalidTree;
            }
			
			if (left != InvalidIndex())
            {
				if (!IsValidIndex(left)) 
					goto InvalidTree;
				if (Parent(left) != curr) 
					goto InvalidTree;
				if (IsRed(curr) && IsRed(left)) 
					goto InvalidTree;
            }
		}
	}
	if (numFree2 != numFree)
		goto InvalidTree;
	
#endif // UTLTREE_PARANOID
	
	return true;
	
InvalidTree:
	return false;
}


//-----------------------------------------------------------------------------
// Sets the less func
//-----------------------------------------------------------------------------

template <class T, class I>  
void CUtlRBTree<T, I>::SetLessFunc( typename CUtlRBTree<T, I>::LessFunc_t func )
{
	if (!m_LessFunc)
		m_LessFunc = func;
	else
	{
		// need to re-sort the tree here....
		Assert(0);
	}
}


//-----------------------------------------------------------------------------
// inserts a node into the tree
//-----------------------------------------------------------------------------

// Inserts a node into the tree, doesn't copy the data in.
template <class T, class I> 
void CUtlRBTree<T, I>::FindInsertionPosition( T const &insert, I &parent, bool &leftchild )
{
	Assert( m_LessFunc != NULL );
	
	/* find where node belongs */
	I current = m_Root;
	parent = InvalidIndex();
	leftchild = false;
	while (current != InvalidIndex()) 
	{
		parent = current;
		if (m_LessFunc( insert, Element(current) ))
		{
			leftchild = true; current = LeftChild(current);
		}
		else
		{
			leftchild = false; current = RightChild(current);
		}
	}
}

template <class T, class I> 
I CUtlRBTree<T, I>::Insert( T const &insert )
{
	// use copy constructor to copy it in
	I parent;
	bool leftchild;
	FindInsertionPosition( insert, parent, leftchild );
	I newNode = InsertAt( parent, leftchild );
	CopyConstruct( &Element( newNode ), insert );
	return newNode;
}


template <class T, class I> 
void CUtlRBTree<T, I>::Insert( const T *pArray, int nItems )
{
	while ( nItems-- )
	{
		Insert( *pArray++ );
	}
}

//-----------------------------------------------------------------------------
// finds a node in the tree
//-----------------------------------------------------------------------------

template <class T, class I> 
I CUtlRBTree<T, I>::Find( T const &search ) const
{
	Assert( m_LessFunc != NULL );
	
	I current = m_Root;
	while (current != InvalidIndex()) 
	{
		if (m_LessFunc( search, Element(current) ))
			current = LeftChild(current);
		else if (m_LessFunc( Element(current), search ))
			current = RightChild(current);
		else 
			break;
	}
	return current;
}

#endif//UTLRBTREE_H
