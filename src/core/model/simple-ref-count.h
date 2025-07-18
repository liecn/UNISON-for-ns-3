/*
 * Copyright (c) 2007 Georgia Tech Research Corporation, INRIA
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Authors: George Riley <riley@ece.gatech.edu>
 *          Gustavo Carneiro <gjcarneiro@gmail.com>,
 *          Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 */
#ifndef SIMPLE_REF_COUNT_H
#define SIMPLE_REF_COUNT_H

#include "assert.h"
#include "atomic-counter.h"
#include "default-deleter.h"

#include <limits>
#include <stdint.h>

/**
 * @file
 * @ingroup ptr
 * ns3::SimpleRefCount declaration and template implementation.
 */

namespace ns3
{

/**
 * @ingroup ptr
 * @brief Empty class, used as a default parent class for SimpleRefCount
 */
class Empty
{
};

/**
 * @ingroup ptr
 * @brief A template-based reference counting class
 *
 * This template can be used to give reference-counting powers
 * to a class. This template does not require this class to
 * have a virtual destructor or a specific (or any) parent class.
 *
 * @note If you are moving to this template from the RefCountBase class,
 * you need to be careful to mark appropriately your destructor virtual
 * if needed. i.e., if your class has subclasses, _do_ mark your destructor
 * virtual.
 *
 *
 * This template takes 3 arguments but only the first argument is
 * mandatory:
 *
 * @tparam T \explicit The typename of the subclass which derives
 *      from this template class. Yes, this is weird but it's a
 *      common C++ template pattern whose name is CRTP (Curiously
 *      Recursive Template Pattern)
 * @tparam PARENT \explicit The typename of the parent of this template.
 *      By default, this typename is "'ns3::Empty'" which is an empty
 *      class: compilers which implement the EBCO optimization (empty
 *      base class optimization) will make this a no-op
 * @tparam DELETER \explicit The typename of a class which implements
 *      a public static method named 'Delete'. This method will be called
 *      whenever the SimpleRefCount template detects that no references
 *      to the object it manages exist anymore.
 *
 * Interesting users of this class include ns3::Object as well as ns3::Packet.
 */
template <typename T, typename PARENT = Empty, typename DELETER = DefaultDeleter<T>>
class SimpleRefCount : public PARENT
{
  public:
    /** Default constructor.  */
    SimpleRefCount()
        : m_count(1)
    {
    }

    /**
     * Copy constructor
     * @param [in] o The object to copy into this one.
     */
    SimpleRefCount(const SimpleRefCount& o [[maybe_unused]])
        : m_count(1)
    {
    }

    /**
     * Assignment operator
     * @param [in] o The object to copy
     * @returns The copy of \pname{o}
     */
    SimpleRefCount& operator=(const SimpleRefCount& o [[maybe_unused]])
    {
        return *this;
    }

    /**
     * Increment the reference count. This method should not be called
     * by user code. SimpleRefCount instances are expected to be used in
     * conjunction with the Ptr template which would make calling Ref
     * unnecessary and dangerous.
     */
    inline void Ref() const
    {
        NS_ASSERT(m_count < std::numeric_limits<uint32_t>::max());
        m_count++;
    }

    /**
     * Decrement the reference count. This method should not be called
     * by user code. SimpleRefCount instances are expected to be used in
     * conjunction with the Ptr template which would make calling Ref
     * unnecessary and dangerous.
     */
    inline void Unref() const
    {
        if (m_count-- == 1)
        {
#ifdef NS3_MTP
            std::atomic_thread_fence(std::memory_order_acquire);
#endif
            DELETER::Delete(static_cast<T*>(const_cast<SimpleRefCount*>(this)));
        }
    }

    /**
     * Get the reference count of the object.
     * Normally not needed; for language bindings.
     *
     * @return The reference count.
     */
    inline uint32_t GetReferenceCount() const
    {
        return m_count;
    }

  private:
    /**
     * The reference count.
     *
     * @internal
     * Note we make this mutable so that the const methods can still
     * change it.
     */
#ifdef NS3_MTP
    mutable AtomicCounter m_count;
#else
    mutable uint32_t m_count;
#endif
};

} // namespace ns3

#endif /* SIMPLE_REF_COUNT_H */
