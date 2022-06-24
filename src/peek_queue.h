

/*
	Very simple queue that allows you to peek ahead of the first position.
	No advanced C++, so no move semantics or things like that supported. It was only made for a single purpose of having a token queue for file parsing.
*/


#ifndef MOBIUS_PEEK_QUEUE_H
#define MOBIUS_PEEK_QUEUE_H


#include "mobius_common.h"
#include "linear_memory.h"


template<typename T>
struct Peek_Queue {
private:
	T      *data;
	size_t  capacity;
	
	s64 cursor_first;
	s64 cursor_end;
public:
	// data[(cursor_first + idx) % capacity]  is item number idx in the queue, idx=0 being the first
	// data[(cursor_end-1) % capacity]        is the last item in the queue
	
	Peek_Queue();
	~Peek_Queue();
	
	void      advance();
	void      reserve(s64 reserve_capacity);
	s64       max_peek();
	const T * peek(s64 peek_ahead);
	      T * append();
};

template<typename T>
Peek_Queue<T>::Peek_Queue() {
	capacity = 16;
	data     = alloc_cleared<T>(capacity);
	
	cursor_first = 0;
	cursor_end   = 0;
}

template<typename T>
Peek_Queue<T>::~Peek_Queue() {
	free(data);
}

template<typename T> void
Peek_Queue<T>::advance()
{
	if(cursor_first < cursor_end)
		++cursor_first;
	else
		fatal_error(Mobius_Error::internal, "Advanced a Peek_Queue beyond the end.");
}

template<typename T> void
Peek_Queue<T>::reserve(s64 reserve_capacity) {
	if(reserve_capacity > (s64)capacity)
	{
		// Make capacity a power of 2 just because.
		size_t new_capacity = capacity*2;
		while(reserve_capacity > (s64)new_capacity) new_capacity *= 2;
		
		T *new_data = alloc_cleared<T>(new_capacity);
		
		// Copy over data to new buffer
		for(s64 idx = 0; idx < (s64)capacity; ++idx)
			new_data[(cursor_first + idx) % new_capacity] = data[(cursor_first + idx) % capacity];
		
		free(data);
		data     = new_data;
		capacity = new_capacity;
	}
}

template<typename T> s64
Peek_Queue<T>::max_peek() {
	return (cursor_end-1) - cursor_first;
}

template<typename T> const T *
Peek_Queue<T>::peek(s64 peek_at) {
	if(peek_at < 0)
		fatal_error(Mobius_Error::internal, "Peeked backwards in a queue.");
	
	if(cursor_first + peek_at >= cursor_end)
		fatal_error(Mobius_Error::internal, "Peeked too far ahead in a queue.");
	
	return &data[(cursor_first + peek_at) % capacity];
}

template<typename T> T *
Peek_Queue<T>::append() {
	s64 need_capacity = cursor_end - cursor_first + 1;
	if(need_capacity > (s64)capacity)
		fatal_error(Mobius_Error::internal, "Appended to queue without reserving enough capacity first.");
	
	return &data[(cursor_end++) % capacity];
}



#endif // MOBIUS_PEEK_QUEUE_H