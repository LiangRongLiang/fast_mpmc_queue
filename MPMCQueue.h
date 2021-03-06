#pragma once

#include <atomic>
#include <cstdint>

template <typename T, size_t cache_line_size = 64> class MPMCQueue
{
public:
	explicit MPMCQueue( size_t capacity )
		: m_items( static_cast<Item*>( _aligned_malloc( sizeof( Item ) * capacity, cache_line_size ) ) )
		, m_capacity( capacity )
		, m_head( 0 )
		, m_tail( 0 )
	{
		for( size_t i = 0; i < capacity; ++i )
		{
			m_items[i].version = i;
		}
	}

	virtual ~MPMCQueue() { _aligned_free( m_items ); }

	// non-copyable
	MPMCQueue( const MPMCQueue<T>& ) = delete;
	MPMCQueue( const MPMCQueue<T>&& ) = delete;
	MPMCQueue<T>& operator=( const MPMCQueue<T>& ) = delete;
	MPMCQueue<T>& operator=( const MPMCQueue<T>&& ) = delete;

	bool try_enqueue( const T& value )
	{
		uint64_t tail = m_tail.load( std::memory_order_relaxed );

		if( m_items[tail % m_capacity].version.load( std::memory_order_acquire ) != tail )
		{
			return false;
		}

		if( !m_tail.compare_exchange_strong( tail, tail + 1, std::memory_order_relaxed ) )
		{
			return false;
		}

		m_items[tail % m_capacity].value = value;

		// Release operation, all reads/writes before this store cannot be reordered past it
		// Writing version to tail + 1 signals reader threads when to read payload
		m_items[tail % m_capacity].version.store( tail + 1, std::memory_order_release );

		return true;
	}

	bool try_dequeue( T& out )
	{
		uint64_t head = m_head.load( std::memory_order_relaxed );

		// Acquire here makes sure read of m_data[head].value is not reordered before this
		// Also makes sure side effects in try_enqueue are visible here
		if( m_items[head % m_capacity].version.load( std::memory_order_acquire ) != (head + 1) )
		{
			return false;
		}

		if( !m_head.compare_exchange_strong( head, head + 1, std::memory_order_relaxed ) )
		{
			return false;
		}

		out = m_items[head % m_capacity].value;

		// This signals to writer threads that they can now write something to this index
		m_items[head % m_capacity].version.store( head + m_capacity, std::memory_order_release );

		return true;
	}

	size_t capacity() const { return m_capacity; }

private:
	struct alignas(cache_line_size)Item
	{
		std::atomic<uint64_t> version;
		T value;
	};

	struct alignas(cache_line_size)AlignedAtomicU64 : public std::atomic<uint64_t>
	{
		using std::atomic<uint64_t>::atomic;
	};
	
	Item* m_items;
	size_t m_capacity;

	// Make sure each index is on a different cache line
	AlignedAtomicU64 m_head;
	AlignedAtomicU64 m_tail;
};
