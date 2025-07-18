/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#define DEBUG_LOG(fmt,...)
#else
#include <string.h>
#include <stdio.h>
#define DEBUG_LOG(fmt,...)
// #define DEBUG_LOG(fmt,...) fprintf( stderr, fmt, ## __VA_ARGS__ )
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset. Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry* aesd_circular_buffer_find_entry_offset_for_fpos(
		struct aesd_circular_buffer* buffer,
    size_t char_offset,
		size_t* entry_offset_byte_rtn
)
{
	size_t pos_bytes = 0;
	unsigned int index = buffer->out_offs;
	do
	{
		if( char_offset < pos_bytes + buffer->entry[index].size ) {
			(*entry_offset_byte_rtn) = char_offset - pos_bytes;
			return &buffer->entry[index];
		}
		pos_bytes += buffer->entry[index].size;
		index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}
	while( index != buffer->out_offs );
	return NULL;
}

int aesd_circular_buffer_fpos_for_entry(
		struct aesd_circular_buffer *buffer,
  	struct aesd_buffer_entry* entry,
		size_t entry_offset,
		size_t* fpos
)
{
	size_t pos_bytes = 0;
	unsigned int index = buffer->out_offs;
	do
	{
		if( &buffer->entry[index] == entry ) {
			if( entry_offset >= entry->size ) {
				return 1;
			}
			(*fpos) = pos_bytes+entry_offset;
			return 0;
		}
		pos_bytes += buffer->entry[index].size;
		index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}
	while( index != buffer->out_offs );
	return 1;
}

unsigned int aesd_circular_buffer_get_count(
		struct aesd_circular_buffer *buffer
)
{
	unsigned int entry_count = 0;
	if( !buffer->full ) {
		if( buffer->out_offs <= buffer->in_offs) {
			entry_count = buffer->in_offs - buffer->out_offs;
		}
		else {
			entry_count = (buffer->in_offs + AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) - buffer->out_offs;
		}
	}
	else {
		// assert( buffer->out_offs == buffer->in_offs );
		entry_count = AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}
	return entry_count;
}

size_t aesd_circular_buffer_get_size(
		struct aesd_circular_buffer *buffer
)
{
	size_t size = 0;
	unsigned int index = buffer->out_offs;
	do
	{
		size += buffer->entry[index].size;
		index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}
	while( index != buffer->out_offs );
	return size;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void aesd_circular_buffer_add_entry(
		struct aesd_circular_buffer* buffer,
		const struct aesd_buffer_entry* add_entry
)
{
	// 1. determine number of entries:
	unsigned int entry_count = aesd_circular_buffer_get_count( buffer );
	DEBUG_LOG( "count: %d\n", entry_count );
	// 2. add element:
	buffer->entry[buffer->in_offs] = (*add_entry);
	buffer->in_offs = (buffer->in_offs + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	// 3. handle corner cases:
	if( entry_count < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) {
		if( entry_count+1 == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED ) {
			buffer->full = true;
		}
	}
	else {
		buffer->out_offs = buffer->in_offs;
	}
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
