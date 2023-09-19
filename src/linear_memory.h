

#ifndef MOBIUS_LINEAR_MEMORY_H
#define MOBIUS_LINEAR_MEMORY_H

#include <stdlib.h>
#include <string.h>
#include <unordered_map>

#include "mobius_common.h"

template<typename T> T*
alloc_cleared(size_t count){
	void *result = malloc(sizeof(T) * count);
	if(!result)
		fatal_error(Mobius_Error::internal, "Ran out of memory.");

	memset(result, 0, sizeof(T) * count);
	return (T *)result;
}


//Array that does not own its data, but instead lives in "linear memory".

template<typename T> struct
Array_View {
	
	T      *data;
	size_t  count;
	
	Array_View() : data(nullptr), count(0) {};
	Array_View(T *data, size_t count) : data(data), count(count) {};
	
	inline T& operator[](size_t index) {
#ifdef MOBIUS_TEST_INDEX_OVERFLOW
		if(index >= count)
			fatal_error(Mobius_Error::internal, "Index overflow when indexing an array.");
#endif
		return data[index];
	}
	inline const T& operator[](size_t index) const { 
#ifdef MOBIUS_TEST_INDEX_OVERFLOW
		if(index >= count)
			fatal_error(Mobius_Error::internal, "Index overflow when indexing an array.");
#endif	
		return data[index];
	}
	
	//NOTE: For C++11 - style iteration
	inline T* begin() { return data; }
	inline T* end()   { return data + count; }
	inline const T* begin() const { return data; }
	inline const T* end()   const { return data + count; }
	
	inline size_t size() { return count; }
};

template<typename T>
bool operator==(const Array_View<T> &a, const Array_View<T> &b) {
	if(a.count != b.count) return false;
	for(size_t idx = 0; idx < a.count; ++idx)
		if(a[idx] != b[idx]) return false;
	return true;
}


//NOTE: This is a length-based string that does not have ownership of its data. Good for making substrings without having to reallocate.

struct String_View : Array_View<char> {

	String_View() : Array_View() {}
	
	String_View(const char *c_string) {
		count = strlen(c_string);
		data  = (char *)c_string;
	}
	
	String_View(const std::string &string) {
		count = string.size();
		data  = (char *)string.data();
	}

	String_View substring(size_t start, size_t count) const {
		String_View result;
		result.data  = data + start;
		result.count = count;
		return result;
	}
	
	bool operator==(const char *c_string) const {
		return *this == String_View(c_string);
	}
	
	bool operator!=(const char *c_string) const {
		return !(*this == c_string);
	}
	
	operator std::string() const { return std::move(std::string(data, data+count)); }
};

inline std::ostream&
operator<<(std::ostream& stream, const String_View& str) {
	stream.write(str.data, str.count);
	return stream;
}

struct String_View_Hash {
    //BKDR Hash algorithm
    int operator()(const String_View &str) const {
        int seed = 131; //31 131 1313 13131131313, etc.
        int hash = 0;
		for(size_t at = 0; at < str.count; ++at)
            hash = (hash * seed) + str.data[at];
        return hash & (0x7FFFFFFF);
    }
};

//template <typename Value_Type>
//using string_map = std::unordered_map<String_View, Value_Type, String_View_Hash>;

inline std::vector<String_View>
split(String_View str, char delim) {
	std::vector<String_View> result;
	int base = 0;
	for(int idx = 0; idx <= str.count; ++idx) {
		if(idx == str.count || str[idx] == delim) {
			if(idx != base)
				result.push_back(str.substring(base, idx-base));
			base = idx+1;
		}
	}
	return std::move(result);
}

/*
	This is an allocator useful for doing temporary allocation of memory, where you don't want to deallocate until you are finished with everything.
	
	It is usually best to set the bucket size so high that you don't need to allocate many buckets in the common use case.
	
	This is a very memory-inefficient implementation if it overflows the buckets a lot, as it does not go back to earlier buckets to see if they have space for a smaller allocation if they have overflowed once. So you typically want large bucket sizes compared to the allocation sizes.
*/
/*
struct
Linear_Allocator {
	
	struct Memory_Bucket {
		u8 *data;
		Memory_Bucket *next;
	};
	
	size_t bucket_size;
	size_t current_used;

	Memory_Bucket *first;
	Memory_Bucket *current;
	
	Linear_Allocator(size_t bucket_size) : bucket_size(bucket_size), current_used(0), first(nullptr), current(nullptr) {
	}
	
	~Linear_Allocator() {
		deallocate_all();
	}
	
	template<typename T> T*
	allocate(size_t count) {
		if(count == 0) return nullptr;
		
		if(bucket_size == 0)
			fatal_error(Mobius_Error::internal, "Tried to allocate from an uninitialized bucket allocator.");
		
		size_t requested_size = sizeof(T) * count;
		//NOTE: Pad to 64-bit aligned just to be safe. Not sure how important this is though. Should we also try to align to cache boundaries?
		size_t lack = 8 - requested_size % 8;
		if(lack != 8) requested_size += lack;
		
		if(!current || current_used + requested_size > bucket_size) {
			while(requested_size > bucket_size)
				//TODO: This is not a very good way of doing it, but in any case we should just initialize with so large a size that this does not become a problem. This is just a safety stopgap.
				//TODO: We should add a way of catching that this happens, in debug mode.
				bucket_size *= 2;
			
			// Previous buckets are nonexisting or full. Create a new bucket
			size_t alloc_size = sizeof(Memory_Bucket) + bucket_size;
			u8 *memory = alloc_cleared<u8>(alloc_size);

			Memory_Bucket * new_bucket = (Memory_Bucket *)memory;
			new_bucket->data = memory + sizeof(Memory_Bucket);
			new_bucket->next = nullptr;
			
			if(current)	current->next = new_bucket;
			else        first         = new_bucket;
			
			current = new_bucket;
			current_used = 0;
		}
		
		T *result = (T *)(current->data + current_used);
		current_used += requested_size;
		
		return result;
	}
	
	template<typename T> T *
	make_new() {
		T *result = allocate<T>(1);
		*result = {};                      //NOTE: this only works for objects with a default constructor, but that is all we want it for.
		return result;
	}
	
	template<typename T> T *
	copy_memory(const T* source, size_t count) {
		if(count == 0) return nullptr;
		T *result = allocate<T>(count);
		memcpy(result, source, count*sizeof(T));
		return result;
	}
	
	template<typename T> Array_View<T>
	new_array(size_t count) {
		Array_View<T> result;
		result.count = count;
		result.data  = allocate<T>(count);
		return result;
	}
	
	template<typename T> Array_View<T>
	copy_array(Array_View<T> source) {
		Array_View<T> result;
		result.count = source.count;
		result.data  = copy_memory<T>(source.data, source.count);
		return result;
	}
	
	template<class Container> Array_View<typename Container::value_type>
	copy_container_to_array(Linear_Allocator *allocator, Container *source) {
		Array_View<typename Container::value_type> result;
		result.count = source->size();
		result.data  = allocate<typename Container::value_type>(result.count);
		size_t idx = 0;
		for(const typename Container::value_type& value : *source)
			result.data[idx++] = value;
		return result;
	}
	
	String_View
	copy_string_view(String_View source) {   //TODO: This should really just be using copy_array
		String_View result;
		result.count = source.count;
		result.data  = copy_memory<char>(source.data, source.count);
		return result;
	}

	void
	deallocate_all() {
		if(!first) return;
		
		Memory_Bucket *bucket = first;
		while(bucket) {
			Memory_Bucket *next = bucket->next;
			free(bucket);
			bucket = next;
		}
		first   = nullptr;
		current = nullptr;
		current_used = 0;
	}
};
*/

#endif // MOBIUS_LINEAR_MEMORY_H
