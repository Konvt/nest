#ifndef NEST_HIVE_HPP
#define NEST_HIVE_HPP

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#ifdef NEST_DEBUG
# include <format>
# if defined( _MSC_VER )
#  define NEST_FORCEINLINE [[msvc::noinline]]
# elif defined( __clang__ )
#  define NEST_FORCEINLINE [[clang::noinline]] inline
# elif defined( __GNUC__ )
#  define NEST_FORCEINLINE [[gnu::noinline]] inline
# else
#  define NEST_FORCEINLINE inline
# endif
#else
# if defined( _MSC_VER )
#  define NEST_FORCEINLINE [[msvc::forceinline]]
# elif defined( __clang__ )
#  define NEST_FORCEINLINE [[clang::always_inline]] inline
# elif defined( __GNUC__ )
#  define NEST_FORCEINLINE [[gnu::always_inline]] inline
# else
#  define NEST_FORCEINLINE inline
# endif
#endif

namespace nest {
  namespace details {
    namespace traits {
      // same as libc++
      template<typename Alloc, typename Tp, typename... Args>
      concept AllocatorTriviallyConstructible =
        !requires( Alloc& alloc ) { alloc.construct( std::declval<Tp*>(), std::declval<Args>()... ); };

      template<typename Alloc, typename Tp>
      concept AllocatorTriviallyDestructible =
        !requires( Alloc& alloc ) { alloc.destroy( std::declval<Tp*>() ); };
    } // namespace traits

    namespace utils {
      template<std::integral Integer, std::size_t Ext>
      constexpr bool all_zero( std::span<Integer, Ext> span ) noexcept
      {
        std::remove_const_t<Integer> result = 0;
        for ( const auto num : span )
          result |= num;
        return result == 0;
      }

      template<typename To, typename From>
      NEST_FORCEINLINE To* launder_as( From ptr ) noexcept
      {
        static_assert( !std::is_void_v<To>, "launder_as cannot produce a void*" );
        using Source = std::pointer_traits<From>::element_type;
        if constexpr ( std::is_same_v<std::decay_t<To>, char>
                       || std::is_same_v<std::decay_t<To>, unsigned char>
                       || std::is_same_v<std::decay_t<To>, std::byte>
                       || std::is_same_v<std::decay_t<To>, std::decay_t<Source>> || requires {
                            requires std::is_integral_v<Source> && std::is_integral_v<To>
                                       && !( std::is_same_v<To, bool> || std::is_same_v<From, bool> );
                            requires std::is_same_v<std::make_unsigned_t<To>, std::make_unsigned_t<Source>>;
                          } )
          return reinterpret_cast<To*>( std::to_address( ptr ) );
        else
          return std::launder( reinterpret_cast<To*>( std::to_address( ptr ) ) );
      }

      /**
       * Converts a pointer-like object `ptr` to a pointer-like type `To`.
       *
       * The conversion is performed as follows:
       * * If `ptr` can be `static_cast` to `To`, that conversion is used.
       * * Otherwise, if `To` is a raw pointer type, the address returned by
       *   `std::to_address(ptr)` is `reinterpret_cast` to `To`.
       * * Otherwise, the address returned by `std::to_address(ptr)` is converted
       *   to `std::pointer_traits<To>::element_type*`, laundered, and dereferenced
       *   to obtain a reference to the target object. That reference is then
       *   passed to `std::pointer_traits<To>::pointer_to` to construct `To`.
       *
       * * In the last case, the storage referenced by `ptr` shall already contain
       *   a valid object of `std::pointer_traits<To>::element_type`, unless that
       *   type is `std::byte`, `char`, or `unsigned char`; otherwise, the behavior
       *   is undefined.
       *
       * * In any case, `ptr` shall not be equal to `nullptr`.
       */
      template<typename To, typename From>
        requires( !std::is_convertible_v<From, To> )
      NEST_FORCEINLINE constexpr To pointer_cast( From ptr )
      { // If the From is a pointer with element_type `void`,
        // use `launder_as` or any other conversion instead of `pointer_cast`.
        static_assert( !std::is_void_v<std::remove_pointer_t<std::decay_t<From>>>,
                       "pointer_cast cannot understand the actual target of a void pointer" );
        using Target = std::pointer_traits<To>::element_type;
        assert( ptr != nullptr );
        // The std::convertible_to will also require implicit conversion,
        // but it's too strict.
        if constexpr ( requires { static_cast<To>( ptr ); } )
          return static_cast<To>( ptr );
        else if constexpr ( requires { static_cast<To>( std::to_address( ptr ) ); } )
          return static_cast<To>( std::to_address( ptr ) );
        else if constexpr ( std::is_pointer_v<To> )
          return reinterpret_cast<To>( std::to_address( ptr ) );
        else if constexpr ( requires { static_cast<Target*>( std::to_address( ptr ) ); } )
          return std::pointer_traits<To>::pointer_to( *static_cast<Target*>( std::to_address( ptr ) ) );
        else
          // We cannot detect whether the `pointer_traits` is a user-provided specialization,
          // so we have to create a fancy pointer following the standard.
          // If so, we can detect whether the `pointer_to` is trivial, then directly invoke the ctor of To.
          // Therefore, we must require that ptr must point to an address where a Target already exists.
          return std::pointer_traits<To>::pointer_to( *launder_as<Target*>( ptr ) );
      }
      // If the From is To, or From can implicitly convert to To, match this overload.
      template<typename To>
      NEST_FORCEINLINE constexpr To pointer_cast( To ptr ) noexcept
      { return ptr; }

      template<typename Act, typename Ex>
      NEST_FORCEINLINE constexpr decltype( auto ) attempt( Act&& action, Ex&& on_exception )
      {
#ifdef __cpp_exceptions
        try {
#endif
          return std::invoke( std::forward<Act>( action ) );
#ifdef __cpp_exceptions
        } catch ( ... ) {
          std::invoke( std::forward<Ex>( on_exception ) );
          throw;
        }
#else
        (void)on_exception;
#endif
      }
      template<typename Act, typename Ex, typename Fin>
      NEST_FORCEINLINE constexpr decltype( auto ) attempt( Act&& action, Ex&& on_exception, Fin&& finally )
      {
        struct AtExit {
          // The destruction of arguments are later than local variables.
          Fin& fin;
          ~AtExit() noexcept( std::is_nothrow_invocable_v<Fin> ) { std::invoke( std::forward<Fin>( fin ) ); }
        } guard { finally };
#ifdef __cpp_exceptions
        try {
#endif
          return std::invoke( std::forward<Act>( action ) );
#ifdef __cpp_exceptions
        } catch ( ... ) {
          std::invoke( std::forward<Ex>( on_exception ) );
          throw;
        }
#else
        (void)on_exception;
#endif
      }

      template<typename Act, typename Fin>
      NEST_FORCEINLINE constexpr decltype( auto ) ensure( Act&& action, Fin&& finally )
      { // we do not call `attempt` here because the reason is the same as above.
        struct AtExit {
          Fin& fin;
          constexpr ~AtExit() noexcept( std::is_nothrow_invocable_v<Fin> )
          { std::invoke( std::forward<Fin>( fin ) ); }
        } guard { finally };
        return std::invoke( std::forward<Act>( action ) );
      }

      template<typename Alloc>
      class AllocatorCell {
        [[no_unique_address]] Alloc alloc_;

        static constexpr bool _is_pocca =
          std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value;
        static constexpr bool _is_pocma =
          std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value;
        static constexpr bool _is_pocs = std::allocator_traits<Alloc>::propagate_on_container_swap::value;

      protected:
        template<typename... Args>
          requires std::is_constructible_v<Alloc, Args...>
        constexpr AllocatorCell( Args&&... args )
          noexcept( std::is_nothrow_constructible<Alloc, Args...>::value )
          : alloc_ { std::forward<Args>( args )... }
        {}

        constexpr AllocatorCell()                  = default;
        constexpr AllocatorCell( AllocatorCell&& ) = default;

        constexpr AllocatorCell( const AllocatorCell& other )
          noexcept( std::is_nothrow_copy_constructible_v<Alloc> )
          : alloc_ { std::allocator_traits<Alloc>::select_on_container_copy_construction( other.alloc_ ) }
        {}

        NEST_FORCEINLINE constexpr void pocca( const Alloc& alloc )
          noexcept( !_is_pocca || std::is_nothrow_copy_assignable_v<Alloc> )
        {
          if constexpr ( _is_pocca )
            alloc_ = alloc;
        }
        NEST_FORCEINLINE constexpr void pocma( Alloc&& alloc )
          noexcept( !_is_pocma || std::is_nothrow_move_assignable_v<Alloc> )
        {
          if constexpr ( _is_pocma )
            alloc_ = std::move( alloc );
        }
        NEST_FORCEINLINE constexpr void pocs( Alloc& alloc )
          noexcept( !_is_pocs || std::is_nothrow_swappable_v<Alloc> )
        {
          using std::swap;
          // Calling swap is UB when the allocator does not satisfy POCS and the allocators are not equal.
          assert( _is_pocs || alloc_ == alloc );
          if constexpr ( _is_pocs )
            swap( alloc_, alloc );
        }

        NEST_FORCEINLINE constexpr Alloc& allocator() & noexcept { return alloc_; }
        NEST_FORCEINLINE constexpr const Alloc& allocator() const& noexcept { return alloc_; }
        NEST_FORCEINLINE constexpr Alloc&& allocator() && noexcept { return std::move( alloc_ ); }
        NEST_FORCEINLINE constexpr const Alloc&& allocator() const&& noexcept { return std::move( alloc_ ); }

      public:
        constexpr ~AllocatorCell() = default;

        AllocatorCell& operator=( const AllocatorCell& ) = delete;
        AllocatorCell& operator=( AllocatorCell&& )      = delete;

        constexpr Alloc get_allocator() const noexcept { return alloc_; }
      };

      template<typename T, typename Field, typename... Fields>
      struct SoALayout {
        using size_type   = std::size_t;
        // Used to control the number of elements in an array for different types
        using scheme_type = std::array<size_type, sizeof...( Fields ) + 2>;

        static constexpr size_type alignment =
          std::max( { alignof( T ), alignof( Field ), alignof( Fields )... } );

        struct alignas( alignment ) value_type {};
        static_assert( std::is_trivially_destructible_v<value_type> );

        template<typename Alloc>
        using rebind_alloc = std::allocator_traits<Alloc>::template rebind_alloc<value_type>;

      private:
        static constexpr size_type align_to( size_type num_bytes, size_type align ) noexcept
        {
          const auto remainder = num_bytes % align;
          return remainder == 0 ? num_bytes : num_bytes + align - remainder;
        }

      public:
        /// @return std::array Return all the SoA array offsets except for T. The offset of T is always 0.
        static constexpr auto split( scheme_type scheme ) noexcept
        {
          std::array<size_type, 1 + sizeof...( Fields )> layout;

          size_type current      = sizeof( T ) * scheme[0];
          size_type index        = 0;
          size_type scheme_index = 1;

          current         = align_to( current, alignof( Field ) );
          layout[index++] = current;
          current += sizeof( Field ) * scheme[scheme_index++];

          ( ( current         = align_to( current, alignof( Fields ) ),
              layout[index++] = current,
              current += sizeof( Fields ) * scheme[scheme_index++] ),
            ... );

          return layout;
        }
        // Precondition:
        // * The allocation size represented by count is representable by `size_type`.
        // Return the allocation size (in bytes).
        static constexpr size_type allocation( scheme_type scheme ) noexcept
        {
          size_type current      = sizeof( T ) * scheme[0];
          size_type scheme_index = 1;

          current = align_to( current, alignof( Field ) );
          current += sizeof( Field ) * scheme[scheme_index++];

          ( ( current = align_to( current, alignof( Fields ) ),
              current += sizeof( Fields ) * scheme[scheme_index++] ),
            ... );

          return align_to( current, alignment );
        }
        template<typename Alloc>
        static constexpr auto allocate( Alloc& alloc, scheme_type scheme )
        {
          static_assert( std::is_same_v<value_type, typename std::allocator_traits<Alloc>::value_type> );
          return std::allocator_traits<Alloc>::allocate( alloc, allocation( scheme ) / sizeof( value_type ) );
        }
        template<typename Alloc>
        static constexpr void deallocate( Alloc& alloc, auto ptr, scheme_type scheme ) noexcept
        {
          static_assert( std::is_same_v<value_type, typename std::allocator_traits<Alloc>::value_type> );
          using UnitPointer = std::allocator_traits<Alloc>::pointer;
          if constexpr ( !std::is_pointer_v<UnitPointer> )
            // to satisfy the requirement of pointer_cast
            std::construct_at( pointer_cast<value_type*>( ptr ) );
          std::allocator_traits<Alloc>::deallocate( alloc,
                                                    pointer_cast<UnitPointer>( ptr ),
                                                    allocation( scheme ) / sizeof( value_type ) );
        }

        /// @return std::array Return all the SoA array offsets except for T. The offset of T is always 0.
        static constexpr auto split( size_type count ) noexcept
        {
          std::array<size_type, sizeof...( Fields ) + 2> scheme;
          std::ranges::fill( scheme, count );
          return split( scheme );
        }
        // Precondition:
        // * The allocation size represented by count is representable by `size_type`.
        // Return the allocation size (in bytes).
        static constexpr size_type allocation( size_type count ) noexcept
        {
          std::array<size_type, sizeof...( Fields ) + 2> scheme;
          std::ranges::fill( scheme, count );
          return allocation( scheme );
        }
        template<typename Alloc>
        static constexpr auto allocate( Alloc& alloc, size_type count )
        {
          static_assert( std::is_same_v<value_type, typename std::allocator_traits<Alloc>::value_type> );
          return std::allocator_traits<Alloc>::allocate( alloc, allocation( count ) / sizeof( value_type ) );
        }
        template<typename Alloc>
        static constexpr void deallocate( Alloc& alloc, auto ptr, size_type count ) noexcept
        {
          static_assert( std::is_same_v<value_type, typename std::allocator_traits<Alloc>::value_type> );
          using UnitPointer = std::allocator_traits<Alloc>::pointer;
          if constexpr ( !std::is_pointer_v<UnitPointer> )
            // to satisfy the requirement of pointer_cast
            std::construct_at( pointer_cast<value_type*>( ptr ) );
          std::allocator_traits<Alloc>::deallocate( alloc,
                                                    pointer_cast<UnitPointer>( ptr ),
                                                    allocation( count ) / sizeof( value_type ) );
        }
      };
    } // namespace utils
  } // namespace details

  struct HiveLimits {
    std::size_t min, max;

    constexpr HiveLimits( std::size_t minimum, std::size_t maximum ) noexcept
      : min { minimum }, max { maximum }
    { assert( min <= max ); }
  };

  template<typename Tp, typename Alloc = std::allocator<Tp>>
  class Hive : public details::utils::AllocatorCell<Alloc> {
    using AllocCell = details::utils::AllocatorCell<Alloc>;
    using Skipfield =
      // The out-of-bounds check in Iterator will rely on the selection criteria of Skipfield.
      std::conditional_t<( sizeof( Tp ) > 8 || alignof( Tp ) > 8 ), std::uint16_t, std::uint8_t>;

    struct Index {
      static constexpr Skipfield npos = ( std::numeric_limits<Skipfield>::max )();

      Skipfield prev = npos;
      Skipfield next = npos;
    };

    union Payload {
      Index index;
      Tp data;

      // The trivial will select the first member as the "active but uninitialized" member
      constexpr Payload() = default;

      constexpr ~Payload()
        requires std::is_trivially_destructible_v<Tp>
      = default;
      constexpr ~Payload() noexcept {}

      Payload( const Payload& )            = delete;
      Payload& operator=( const Payload& ) = delete;

      constexpr Payload( Index idx ) noexcept : index { idx } {}
      constexpr Payload( const Tp& item ) noexcept( std::is_nothrow_copy_constructible_v<Tp> ) : data { item }
      {}
      constexpr Payload( Tp&& item ) noexcept( std::is_nothrow_move_constructible_v<Tp> )
        : data { std::move( item ) }
      {}

      template<typename... Args>
      // Avoid ambiguity with the default constructor
      constexpr Payload( std::in_place_t, Args&&... args )
        noexcept( std::is_nothrow_constructible_v<Tp, Args...> )
        : data { std::forward<Args>( args )... }
      { static_assert( std::is_constructible_v<Tp, Args...> ); }
    };

    using Layout    = details::utils::SoALayout<Payload, Skipfield>;
    using AreaAlloc = Layout::template rebind_alloc<Alloc>;
    using Area =
      std::pointer_traits<typename std::allocator_traits<AreaAlloc>::pointer>::template rebind<std::byte>;

    struct Block;
    using BlockAlloc = std::allocator_traits<Alloc>::template rebind_alloc<Block>;
    using Unit       = std::allocator_traits<BlockAlloc>::pointer;

    // This type is merely used as a parameter and has no actual meaning.
    struct BiList {
      Unit head = nullptr;
      Unit tail = nullptr;
    };

    // Even if we use a SoALayout calculator, since the alignment of Skipfield is either 1 or 2,
    // and the alignment of Skipfield is 2 only when the alignment of Tp exceeds 8,
    // the skipfield array must be tightly adjacent to the element array—meaning
    // there is **0-byte** padding (see method `allocate`).
    // Therefore, if the address of an element pointer is greater than or equal to the address of skipfield,
    // it can be considered as exceeding the representation range of the current block.
    // This design also comes from the plf::hive.
    struct Block {
      Area skipfield = nullptr;
      Area element   = nullptr;

      Skipfield first_hole = Index::npos;
      Skipfield occupied   = 0;
      Skipfield capacity   = 0;

      // nullptr marks the front of the block chain.
      // This field in the first node will always point to the tail node.
      Unit prev = nullptr;
      // nullptr marks the end of the block chain.
      Unit next = nullptr;

      // nullptr marks the front of the hollow chain.
      Unit prev_hollow = nullptr;
      // nullptr marks the end of the hollow chain.
      Unit next_hollow = nullptr;
    };

    NEST_FORCEINLINE static Payload& as_payload( Area ptr ) noexcept
    { return *details::utils::launder_as<Payload>( ptr ); }
    NEST_FORCEINLINE static Tp& as_data( Area ptr ) noexcept { return as_payload( ptr ).data; }
    NEST_FORCEINLINE static Index& as_index( Area ptr ) noexcept { return as_payload( ptr ).index; }
    NEST_FORCEINLINE static Skipfield& as_skipfield( Area ptr ) noexcept
    { return *details::utils::launder_as<Skipfield>( ptr ); }

    template<typename U = std::byte>
    NEST_FORCEINLINE static constexpr Area address_at(
      Area origin,
      typename std::allocator_traits<AreaAlloc>::difference_type offset ) noexcept
    { return origin + offset * sizeof( U ); }
    template<typename U>
    NEST_FORCEINLINE static U* as_address(
      Area origin,
      typename std::allocator_traits<AreaAlloc>::difference_type offset ) noexcept
    { return details::utils::pointer_cast<U*>( address_at<U>( origin, offset ) ); }

    NEST_FORCEINLINE static Index& index_at( Unit block, Skipfield pos ) noexcept
    { return as_index( address_at<Payload>( block->element, pos ) ); }
    NEST_FORCEINLINE static Skipfield& skipfield_at( Unit block, Skipfield pos ) noexcept
    { return as_skipfield( address_at<Skipfield>( block->skipfield, pos ) ); }

    static constexpr auto max_default_capacity() noexcept
    { // Capacity limit calculation follows the reference implementation of P0447 (plf::hive).
      return static_cast<Skipfield>(
        (std::min<size_type>)( { 8192u,
                                 ( std::numeric_limits<Skipfield>::max )() - 1u,
                                 std::allocator_traits<Alloc>::max_size( {} ) } ) );
    }
    static constexpr auto min_default_capacity() noexcept
    {
      return (std::max<Skipfield>)( 8u,
                                    (std::min<size_type>)( ( ( sizeof( Hive ) + sizeof( Block ) ) * 2 )
                                                             / sizeof( Payload ),
                                                           max_default_capacity() ) );
    }

    template<bool Const>
    class Iterator {
      friend class Filler;
      friend Hive;

      Area element_   = nullptr;
      Area skipfield_ = nullptr;
      Unit block_     = nullptr;

      // Detect whether the iterator is at the end of the current block.
      NEST_FORCEINLINE constexpr bool exhausted() const noexcept { return element_ >= block_->skipfield; }
      // The rest length of the current block.
      NEST_FORCEINLINE constexpr Skipfield rest() const noexcept
      { // Regarding the alignment and padding filling issues,
        // please refer to the comments of the `allocate` method for details.
        assert( exhausted() == false );
        return static_cast<Skipfield>( ( block_->skipfield - element_ ) / sizeof( Payload ) );
      }
      NEST_FORCEINLINE constexpr Skipfield offset() const noexcept
      { return static_cast<Skipfield>( ( skipfield_ - block_->skipfield ) / sizeof( Skipfield ) ); }

      // This method will not automatically advance the iterator to the next block.
      // And it will return a boolean indicating whether the iterator is still valid.
      bool advance() noexcept
      { // Regarding the reason why "incrementing when pointing to the last element of the array
        // does not cause the skipfield_ pointer to go out of bounds",
        // please refer to the comments within the `allocate` method.
        skipfield_ = address_at<Skipfield>( skipfield_, 1 );
        element_   = address_at<Payload>( element_, as_skipfield( skipfield_ ) + 1 );
        if ( !exhausted() ) [[likely]] {
          skipfield_ = address_at<Skipfield>( skipfield_, as_skipfield( skipfield_ ) );
          return true;
        }
        return false;
      }

      // Advance the iterator by the sepcified number of steps, it will automatically skip over the hole.
      // This function **assumes** that:
      // the target position must be either a hole endpoint, a sentinel position, or a valid element.
      bool advance( Skipfield step ) noexcept
      { // internal tool
        skipfield_ = address_at<Skipfield>( skipfield_, step );
        element_   = address_at<Payload>( element_, step + as_skipfield( skipfield_ ) );
        skipfield_ = address_at<Skipfield>( skipfield_, as_skipfield( skipfield_ ) );
        return !exhausted();
      }

      // Step the iterator to the next block.
      bool next() noexcept
      {
        if ( block_->next != nullptr ) {
          block_     = block_->next;
          element_   = address_at<Payload>( block_->element, as_skipfield( block_->skipfield ) );
          skipfield_ = address_at<Skipfield>( block_->skipfield, as_skipfield( block_->skipfield ) );
          return true;
        }
        return false;
      }

      // Get the mutable reference of the data.
      NEST_FORCEINLINE Tp& mut() const noexcept { return as_data( element_ ); }

      constexpr Iterator( Area element, Area skipfield, Unit block ) noexcept
        : element_ { element }, skipfield_ { skipfield }, block_ { block }
      {}

      template<bool C>
      NEST_FORCEINLINE static constexpr Iterator from( Iterator<C> other ) noexcept
      { return { other.element_, other.skipfield_, other.block_ }; }

      NEST_FORCEINLINE static Iterator start_of( Unit block ) noexcept
      {
        return { address_at<Payload>( block->element, as_skipfield( block->skipfield ) ),
                 address_at<Skipfield>( block->skipfield, as_skipfield( block->skipfield ) ),
                 block };
      }
      NEST_FORCEINLINE static Iterator sentinel_of( Unit block ) noexcept
      { return { block->skipfield, address_at<Skipfield>( block->skipfield, block->capacity ), block }; }

    public:
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type        = std::conditional_t<Const, const Tp, Tp>;
      using pointer           = value_type*;
      using reference         = value_type&;
      using difference_type   = std::allocator_traits<AreaAlloc>::difference_type;

      constexpr Iterator() = default;

      pointer operator->() const noexcept { return std::addressof( mut() ); }
      reference operator*() const noexcept { return mut(); }

      Iterator& operator++() noexcept
      {
        if ( !advance() )
          next();
        return *this;
      }
      Iterator operator++( int ) noexcept
      {
        auto copy = *this;
        operator++();
        return copy;
      }

      Iterator& operator--() noexcept
      {
        assert( block_ != nullptr );
        skipfield_ -= sizeof( Skipfield );
        if ( skipfield_ >= block_->skipfield ) {
          element_ -= ( static_cast<size_type>( as_skipfield( skipfield_ ) ) + 1 ) * sizeof( Payload );
          skipfield_ -= as_skipfield( skipfield_ ) * sizeof( Skipfield );
          if ( skipfield_ >= block_->skipfield )
            return *this;
        }

        block_                    = block_->prev;
        const auto prev_skipfield = address_at<Skipfield>( block_->skipfield, block_->capacity - 1 );
        const auto skipped_bytes  = as_skipfield( prev_skipfield ) * sizeof( Payload );
        element_                  = block_->skipfield - skipped_bytes - sizeof( Payload );
        skipfield_                = prev_skipfield - skipped_bytes;
        return *this;
      }
      Iterator operator--( int ) noexcept
      {
        auto copy = *this;
        operator--();
        return copy;
      }

      friend constexpr bool operator==( const Iterator& a, const Iterator& b ) noexcept
      { return a.element_ == b.element_; }
      friend constexpr bool operator!=( const Iterator& a, const Iterator& b ) noexcept
      { return !( a == b ); }
      friend constexpr bool operator==( const Iterator& a, const Iterator<!Const>& b ) noexcept
      { return a.element_ == b.element_; }
      friend constexpr bool operator!=( const Iterator& a, const Iterator<!Const>& b ) noexcept
      { return !( a == b ); }

      // to support conversion
      NEST_FORCEINLINE constexpr operator Iterator<true>() const noexcept
        requires( !Const )
      { return { element_, skipfield_, block_ }; }
    };

    // An iterator used to traverse empty blocks.
    class Filler {
      friend Hive;

    protected:
      Area element_   = nullptr;
      Area skipfield_ = nullptr;
      Unit block_     = nullptr;
      Skipfield rest_ = 0; // the rest length of this hole

      constexpr Filler( Area element, Area skipfield, Unit block ) noexcept
        : element_ { element }, skipfield_ { skipfield }, block_ { block }
      {}

    public:
      NEST_FORCEINLINE static constexpr Filler from( Unit blank ) noexcept
      {
        assert( blank->occupied == 0 );
        Filler cursor { blank->element, blank->skipfield, blank };
        cursor.rest_ = blank->capacity;
        return cursor;
      }

      template<typename... Args>
      NEST_FORCEINLINE void occupy( Alloc& alloc, Args&&... args ) const
        noexcept( std::is_nothrow_constructible_v<Tp, Args...> )
      {
        static_assert( std::is_constructible_v<Tp, Args...> );
        std::allocator_traits<Alloc>::construct( alloc,
                                                 details::utils::pointer_cast<Payload*>( element_ ),
                                                 std::in_place,
                                                 std::forward<Args>( args )... );
        as_skipfield( skipfield_ ) = 0;
      }

      NEST_FORCEINLINE constexpr Skipfield rest() const noexcept { return rest_; }
      NEST_FORCEINLINE constexpr Skipfield offset() const noexcept
      { return static_cast<Skipfield>( ( skipfield_ - block_->skipfield ) / sizeof( Skipfield ) ); }

      constexpr bool advance() noexcept
      {
        if ( --rest_ > 0 ) {
          element_   = address_at<Payload>( element_, 1 );
          skipfield_ = address_at<Skipfield>( skipfield_, 1 );
          return true;
        }
        return false;
      }

      // Take `step` steps forward in the current hole.
      constexpr bool advance( Skipfield step ) noexcept
      {
        assert( step <= rest_ );
        rest_ -= step;
        if ( rest_ > 0 ) {
          element_   = address_at<Payload>( element_, step );
          skipfield_ = address_at<Skipfield>( skipfield_, step );
          return true;
        }
        return false;
      }

      template<bool C>
      NEST_FORCEINLINE constexpr explicit operator Iterator<C>() const noexcept
      { return { element_, skipfield_, block_ }; }
    };

    // Iterator types for holes will consume the current hole during stepping (i.e. remove it).
    class Muncher : public Filler {
      friend Hive;

      Index index_;

      using Filler::Filler;

    public:
      constexpr Muncher() = default;

      NEST_FORCEINLINE static Muncher from( Unit hollow ) noexcept
      {
        assert( hollow->occupied < hollow->capacity );
        Muncher cursor { address_at<Payload>( hollow->element, hollow->first_hole ),
                         address_at<Skipfield>( hollow->skipfield, hollow->first_hole ),
                         hollow };
        cursor.rest_  = as_skipfield( cursor.skipfield_ );
        cursor.index_ = as_index( cursor.element_ );
        cursor.element_ -= ( cursor.rest_ - 1 ) * sizeof( Payload );
        cursor.skipfield_ -= ( cursor.rest_ - 1 ) * sizeof( Skipfield );
        return cursor;
      }

      bool advance() noexcept
      {
        if ( --this->rest_ > 0 ) {
          this->element_   = address_at<Payload>( this->element_, 1 );
          this->skipfield_ = address_at<Skipfield>( this->skipfield_, 1 );
          return true;
        }
        remove_hole( this->block_, index_ );
        if ( index_.next != Index::npos ) {
          this->element_   = address_at<Payload>( this->block_->element, index_.next );
          this->skipfield_ = address_at<Skipfield>( this->block_->skipfield, index_.next );
          this->rest_      = as_skipfield( this->skipfield_ );
          assert( this->rest_ > 0 );
          index_ = as_index( address_at<Payload>( this->element_, this->rest_ - 1 ) );
          this->element_ -= ( this->rest_ - 1 ) * sizeof( Payload );
          this->skipfield_ -= ( this->rest_ - 1 ) * sizeof( Skipfield );
          return true;
        }
        return false;
      }

      // Take `step` steps forward in the current hole.
      bool advance( Skipfield step ) noexcept
      {
        assert( step <= this->rest_ );
        this->rest_ -= step;
        if ( this->rest_ > 0 ) {
          this->element_   = address_at<Payload>( this->element_, step );
          this->skipfield_ = address_at<Skipfield>( this->skipfield_, step );
          return true;
        }
        remove_hole( this->block_, index_ );
        if ( index_.next != Index::npos ) {
          this->element_   = address_at<Payload>( this->block_->element, index_.next );
          this->skipfield_ = address_at<Skipfield>( this->block_->skipfield, index_.next );
          this->rest_      = as_skipfield( this->skipfield_ );
          assert( this->rest_ > 0 );
          index_ = as_index( address_at<Payload>( this->element_, this->rest_ - 1 ) );
          this->element_ -= ( this->rest_ - 1 ) * sizeof( Payload );
          this->skipfield_ -= ( this->rest_ - 1 ) * sizeof( Skipfield );
          return true;
        }
        return false;
      }
    };

    // An iterator that projects the type `Payload&` to `Tp&`.
    class Accessor {
      Payload* current_;

    public:
      constexpr Accessor( Payload* current ) noexcept : current_ { current } {}

      NEST_FORCEINLINE static constexpr Accessor from_address( Area ptr ) noexcept
      {
        assert( ptr != nullptr );
        return { details::utils::pointer_cast<Payload*>( ptr ) };
      }
      NEST_FORCEINLINE static Accessor from_object( Area ptr ) noexcept
      {
        assert( ptr != nullptr );
        return { details::utils::launder_as<Payload>( ptr ) };
      }

      using iterator_category = std::contiguous_iterator_tag;
      using value_type        = Tp;
      using pointer           = value_type*;
      using reference         = value_type&;
      using difference_type   = std::ptrdiff_t;

      constexpr pointer operator->() const noexcept { return std::addressof( current_->data ); }
      constexpr reference operator*() const noexcept { return current_->data; }
      constexpr reference operator[]( difference_type dist ) const noexcept { return current_[dist]; }

      constexpr Accessor& operator++() noexcept
      {
        ++current_;
        return *this;
      }
      constexpr Accessor operator++( int ) noexcept
      {
        auto copy = *this;
        operator++();
        return copy;
      }

      constexpr Accessor& operator--() noexcept
      {
        --current_;
        return *this;
      }
      constexpr Accessor operator--( int ) noexcept
      {
        auto copy = *this;
        operator--();
        return copy;
      }

      friend constexpr Accessor operator+( Accessor iter, difference_type dist ) noexcept
      { return { iter.current_ + dist }; }
      friend constexpr Accessor operator+( difference_type dist, Accessor iter ) noexcept
      { return { iter.current_ + dist }; }
      friend constexpr Accessor operator-( Accessor iter, difference_type dist ) noexcept
      { return { iter.current_ - dist }; }
      friend constexpr difference_type operator-( Accessor a, Accessor b ) noexcept
      { return { a.current_ - b.current_ }; }

      friend constexpr Accessor& operator+=( Accessor& iter, difference_type dist ) noexcept
      {
        iter.current_ += dist;
        return iter;
      }
      friend constexpr Accessor& operator-=( Accessor& iter, difference_type dist ) noexcept
      {
        iter.current_ -= dist;
        return iter;
      }

      friend constexpr auto operator<=>( const Accessor&, const Accessor& ) = default;
      friend constexpr bool operator==( const Accessor&, const Accessor& )  = default;
      friend constexpr bool operator!=( const Accessor&, const Accessor& )  = default;
    };

  public:
    [[nodiscard]] static constexpr HiveLimits block_capacity_default_limits() noexcept
    { return { min_default_capacity(), max_default_capacity() }; }
    [[nodiscard]] static constexpr HiveLimits block_capacity_hard_limits() noexcept
    {
      return { 3,
               (std::min<size_type>)( ( std::numeric_limits<Skipfield>::max )(),
                                      std::allocator_traits<Alloc>::max_size( {} ) ) };
    }
    [[nodiscard]] static constexpr bool is_within_hard_limits( HiveLimits limits ) noexcept
    {
      constexpr auto hard_limit = block_capacity_hard_limits();
      return hard_limit.min <= limits.min && limits.min <= limits.max && limits.max <= hard_limit.max;
    }

    using value_type      = Tp;
    using allocator_type  = Alloc;
    using pointer         = std::allocator_traits<Alloc>::pointer;
    using const_pointer   = std::allocator_traits<Alloc>::const_pointer;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using size_type       = std::allocator_traits<Alloc>::size_type;
    using difference_type = std::allocator_traits<Alloc>::difference_type;

    constexpr Hive( HiveLimits block_limits, const Alloc& alloc ) noexcept
      : AllocCell( alloc )
      , max_capacity_ { static_cast<Skipfield>( block_limits.max ) }
      , min_capacity_ { static_cast<Skipfield>( block_limits.min ) }
      , next_capacity_ { static_cast<Skipfield>( block_limits.min ) }
    { assert( min_capacity_ <= max_capacity_ ); }
    constexpr explicit Hive( const Alloc& alloc ) noexcept
      : Hive( HiveLimits( min_default_capacity(), max_default_capacity() ), alloc )
    {}

    Hive( size_type count, HiveLimits block_limits, const Alloc& alloc = Alloc() )
      : Hive( block_limits, alloc )
    {
      if ( count > 0 ) {
        BlockAlloc block_alloc { this->allocator() };
        AreaAlloc area_alloc { this->allocator() };
        auto [blanks, total_capacity] = reserve_for( block_alloc, area_alloc, count, block_limits );
        details::utils::attempt(
          [&] {
            do {
              const auto num_constructed = (std::min<size_type>)( count, blanks->capacity );
              if constexpr ( details::traits::AllocatorTriviallyConstructible<Alloc, Tp>
                             && (details::traits::AllocatorTriviallyDestructible<Alloc, Tp>
                                 || std::is_nothrow_default_constructible_v<Tp>))
                std::uninitialized_fill_n( details::utils::pointer_cast<Payload*>( blanks->element ),
                                           num_constructed,
                                           std::in_place );
              else {
                auto target = details::utils::pointer_cast<Payload*>( blanks->element );
                details::utils::attempt(
                  [&] {
                    Skipfield total_constructed = 0;
                    do {
                      std::allocator_traits<Alloc>::construct( this->allocator(), target, std::in_place );
                      ++target;
                      ++total_constructed;
                    } while ( total_constructed < num_constructed );
                  },
                  [&, origin = target] {
                    if ( origin != target )
                      purge( Accessor( std::launder( origin ) ), Accessor( target ) );
                  } );
              }
              std::memset( details::utils::pointer_cast<void*>( blanks->skipfield ),
                           0,
                           num_constructed * sizeof( Skipfield ) );
              count -= num_constructed;
              blanks->occupied = static_cast<Skipfield>( num_constructed );
              occupied_ += num_constructed;
              capacity_ += blanks->capacity;
              if ( num_constructed < blanks->capacity ) {
                build_hole( blanks, num_constructed, blanks->capacity - num_constructed );
                add_hollow( hollow_, blanks );
              }
              attach( head_, detach( blanks ) );
            } while ( count > 0 );
          },
          [&] {
            if ( blanks != nullptr )
              discard( block_alloc, area_alloc, blanks );
            eliminate( block_alloc, area_alloc, head_ );
            occupied_ = 0;
            capacity_ = 0;
          } );
      }
    }
    explicit Hive( size_type count, const Alloc& alloc = Alloc() )
      : Hive( count, HiveLimits( min_default_capacity(), max_default_capacity() ), alloc )
    {}

    Hive( size_type count, const Tp& value, HiveLimits block_limits, const Alloc& alloc = Alloc() )
      : Hive( block_limits, alloc )
    { insert( count, value ); }
    Hive( size_type count, const Tp& value, const Alloc& alloc = Alloc() )
      : Hive( count, value, HiveLimits( min_default_capacity(), max_default_capacity() ), alloc )
    {}

    template<std::input_iterator InputIter>
    Hive( InputIter first, InputIter last, HiveLimits block_limits, const Alloc& alloc = Alloc() )
      : Hive( block_limits, alloc )
    { insert( first, last ); }
    template<std::input_iterator InputIter>
    Hive( InputIter first, InputIter last, const Alloc& alloc = Alloc() )
      : Hive( first, last, HiveLimits( min_default_capacity(), max_default_capacity() ), alloc )
    {}

#ifdef __cpp_lib_containers_ranges
    template<std::ranges::input_range R>
      requires std::convertible_to<std::ranges::range_reference_t<R>, Tp>
    Hive( std::from_range_t, R&& rg, HiveLimits block_limits, const Alloc& alloc = Alloc() )
      : Hive( block_limits, alloc )
    { insert_range( std::forward<R>( rg ) ); }
    template<std::ranges::input_range R>
      requires std::convertible_to<std::ranges::range_reference_t<R>, Tp>
    Hive( std::from_range_t, R&& rg, const Alloc& alloc = Alloc() )
      : Hive( std::from_range,
              std::forward<R>( rg ),
              HiveLimits( min_default_capacity(), max_default_capacity() ),
              alloc )
    {}
#endif

    constexpr explicit Hive( HiveLimits block_limits ) : Hive( block_limits, Alloc() ) {}
    constexpr Hive() noexcept( noexcept( Alloc() ) ) : Hive( Alloc() ) {}

    Hive( const Hive& other, const std::type_identity_t<Alloc>& alloc ) : Hive( alloc )
    {
      if ( other.head_ != nullptr ) {
        BlockAlloc block_alloc { this->allocator() };
        AreaAlloc area_alloc { this->allocator() };
        auto [list, new_capacity] = rebuild_from( block_alloc,
                                                  area_alloc,
                                                  other.cbegin(),
                                                  other.size(),
                                                  HiveLimits( min_capacity_, max_capacity_ ) );
        if ( list->occupied < list->capacity )
          add_hollow( hollow_, list );
        head_     = list;
        occupied_ = other.size();
        capacity_ = new_capacity;
      }
    }
    Hive( const Hive& other )
      : Hive( other,
              std::allocator_traits<Alloc>::select_on_container_copy_construction( other.allocator() ) )
    {}

    Hive( Hive&& rhs, const std::type_identity_t<Alloc>& alloc )
      noexcept( std::allocator_traits<Alloc>::is_always_equal::value )
      : Hive( alloc )
    {
      if ( std::allocator_traits<Alloc>::is_always_equal::value || this->allocator() == rhs.allocator() )
        interchange( rhs );
      else if ( rhs.head_ != nullptr ) {
        assert( rhs.size() > 0 );
        BlockAlloc block_alloc { this->allocator() };
        AreaAlloc area_alloc { this->allocator() };
        auto [list, new_capacity] = rebuild_from( block_alloc,
                                                  area_alloc,
                                                  rhs.begin(),
                                                  rhs.size(),
                                                  HiveLimits( min_capacity_, max_capacity_ ) );
        if ( list->occupied < list->capacity )
          add_hollow( hollow_, list );
        head_     = list;
        occupied_ = rhs.size();
        capacity_ = new_capacity;
      }
    }
    Hive( Hive&& rhs ) noexcept : AllocCell( std::move( rhs ) ) { interchange( rhs ); }

    Hive( std::initializer_list<Tp> ilist, const Alloc& alloc = Alloc() )
      : Hive( ilist.begin(), ilist.end(), alloc )
    {}
    Hive( std::initializer_list<Tp> ilist, HiveLimits block_limits, const Alloc& alloc = Alloc() )
      : Hive( ilist.begin(), ilist.end(), block_limits, alloc )
    {}

    ~Hive() noexcept
    {
      if ( capacity_ > 0 ) {
        BlockAlloc block_alloc { this->allocator() };
        AreaAlloc area_alloc { this->allocator() };
        if ( vacuum_ != nullptr )
          discard( block_alloc, area_alloc, vacuum_ );
        if ( head_ != nullptr )
          eliminate( block_alloc, area_alloc, head_ );
      }
    }

    Hive& operator=( const Hive& other )
    { // The standard said "All elements in *this are either copy-assigned to, or destroyed."
      clear();
      this->pocca( other.allocator() );
      fill_from( other.cbegin(), other.size() );
      return *this;
    }
    Hive& operator=( Hive&& rhs )
      noexcept( std::allocator_traits<Alloc>::is_always_equal::value
                || std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value )
    {
      clear();
      this->pocma( std::move( rhs.allocator() ) );
      if ( std::allocator_traits<Alloc>::is_always_equal::value
           || std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value
           || this->allocator() == rhs.allocator() )
        interchange( rhs );
      else {
        fill_from( rhs.begin(), rhs.size() );
        rhs.clear();
      }
      return *this;
    }
    Hive& operator=( std::initializer_list<Tp> ilist )
    {
      assign( ilist );
      return *this;
    }

    void assign( size_type count, const Tp& value )
    {
      clear();
      insert( count, value );
    }
    template<typename InputIt>
    void assign( InputIt first, InputIt last )
    {
      clear();
      insert( first, last );
    }
    void assign( std::initializer_list<Tp> ilist ) { assign( ilist.begin(), ilist.end() ); }

#ifdef __cpp_lib_containers_ranges
    template<std::ranges::input_range R>
      requires std::convertible_to<std::ranges::range_reference_t<R>, Tp>
    void assign_range( R&& rg )
    {
      clear();
      insert_range( std::forward<R>( rg ) );
    }
#endif

    using iterator               = Iterator<false>;
    using const_iterator         = Iterator<true>;
    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    [[nodiscard]] const_iterator cbegin() const noexcept
    {
      if ( head_ == nullptr ) [[unlikely]]
        return {};
      return const_iterator::start_of( head_ );
    }
    [[nodiscard]] const_iterator begin() const noexcept { return cbegin(); }
    [[nodiscard]] iterator begin() noexcept { return iterator::from( cbegin() ); }

    [[nodiscard]] const_iterator cend() const noexcept
    {
      if ( head_ == nullptr ) [[unlikely]]
        return {};
      assert( head_->prev != nullptr );
      return const_iterator::sentinel_of( head_->prev );
    }
    [[nodiscard]] const_iterator end() const noexcept { return cend(); }
    [[nodiscard]] iterator end() noexcept { return iterator::from( cend() ); }

    [[nodiscard]] const_reverse_iterator crbegin() const noexcept
    { return std::make_reverse_iterator( cend() ); }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept
    { return std::make_reverse_iterator( cend() ); }
    [[nodiscard]] reverse_iterator rbegin() noexcept { return std::make_reverse_iterator( end() ); }

    [[nodiscard]] const_reverse_iterator crend() const noexcept
    { return std::make_reverse_iterator( cbegin() ); }
    [[nodiscard]] const_reverse_iterator rend() const noexcept
    { return std::make_reverse_iterator( cbegin() ); }
    [[nodiscard]] reverse_iterator rend() noexcept { return std::make_reverse_iterator( begin() ); }

    [[nodiscard]] const_iterator get_iterator( const_pointer obj ) const noexcept
    {
      assert( head_ != nullptr );
      assert( obj != nullptr );
      const auto addr = details::utils::pointer_cast<const std::byte*>( obj );
      auto block      = head_;
      while ( addr < std::to_address( block->element ) || addr >= std::to_address( block->skipfield ) )
        // For the safety of the last comparison, please refer to the comments in `Iterator::exhaust`.
        block = block->next;
      assert( block != nullptr );
      const auto offset = addr - std::to_address( block->element );
      // the unit of offset is byte
      return { block->element + offset, block->skipfield + offset, block };
    }
    [[nodiscard]] iterator get_iterator( const_pointer obj ) noexcept
    { return iterator::from( const_cast<const Hive*>( this )->get_iterator( obj ) ); }

    [[nodiscard]] bool empty() const noexcept { return occupied_ == 0; }
    [[nodiscard]] size_type size() const noexcept { return occupied_; }
    [[nodiscard]] size_type capacity() const noexcept { return capacity_; }
    [[nodiscard]] size_type max_size() const noexcept
    { return std::allocator_traits<Alloc>::max_size( this->allocator() ); }

    [[nodiscard]] HiveLimits block_capacity_limits() const noexcept
    { return { min_capacity_, max_capacity_ }; }

    template<typename... Args>
    iterator emplace( Args&&... args )
    {
      iterator result;
      if ( hollow_ != nullptr ) {
        auto hole = Muncher::from( hollow_ );
        hole.occupy( this->allocator(), std::forward<Args>( args )... );
        ++hole.block_->occupied;
        if ( hole.advance() )
          fix_hole( hole.skipfield_, hole.rest() );
        else {
          assert( hole.block_->occupied == hole.block_->capacity );
          remove_hollow( hollow_, hole.block_ );
        }
        result = static_cast<iterator>( hole );
      } else {
        // try to re-use the idle blocks.
        if ( vacuum_ != nullptr ) {
          result = fill_blank( vacuum_, std::forward<Args>( args )... );
          add_hollow( hollow_, vacuum_ );
          attach( head_, detach( vacuum_ ) );
        } else {
          // allocate one
          BlockAlloc block_alloc { this->allocator() };
          AreaAlloc area_alloc { this->allocator() };
          assert( next_capacity_ >= min_capacity_ );
          auto blank     = allocate( block_alloc, area_alloc, next_capacity_ );
          next_capacity_ = grow( next_capacity_ );
          details::utils::attempt( [&] { result = fill_blank( blank, std::forward<Args>( args )... ); },
                                   [&] { deallocate( block_alloc, area_alloc, blank ); } );
          capacity_ += blank->capacity;
          add_hollow( hollow_, blank );
          attach( head_, blank );
        }
      }

      ++occupied_;
      return result;
    }
    template<typename... Args>
    NEST_FORCEINLINE iterator emplace_hint( const_iterator, Args&&... args )
    { return emplace( std::forward<Args>( args )... ); }

    NEST_FORCEINLINE iterator insert( const Tp& value ) { return emplace( value ); }
    NEST_FORCEINLINE iterator insert( Tp&& value ) { return emplace( std::move( value ) ); }

    NEST_FORCEINLINE iterator insert( const_iterator, const Tp& value ) { return emplace( value ); }
    NEST_FORCEINLINE iterator insert( const_iterator, Tp&& value ) { return emplace( std::move( value ) ); }

    template<std::input_iterator InputIter>
    NEST_FORCEINLINE void insert( InputIter first, InputIter last )
    {
      if ( first == last ) [[unlikely]]
        return;
      if constexpr ( std::forward_iterator<InputIter> )
        introduce( first, static_cast<size_type>( std::distance( first, last ) ) );
      else
        introduce( first, last );
    }

    NEST_FORCEINLINE void insert( std::initializer_list<Tp> ilist ) { insert( ilist.begin(), ilist.end() ); }
    NEST_FORCEINLINE void insert( size_type count, const Tp& value )
    {
      if ( count == 0 ) [[unlikely]]
        return;
      introduce( value, count );
    }

#ifdef __cpp_lib_containers_ranges
    template<std::ranges::input_range R>
      requires std::convertible_to<std::ranges::range_reference_t<R>, Tp>
    NEST_FORCEINLINE void insert_range( R&& rg )
    {
      if constexpr ( std::ranges::sized_range<std::decay_t<R>> )
        introduce( std::ranges::begin( rg ), std::ranges::size( rg ) );
      else // To prevent the difference between the sentinel type and the iterator type.
        introduce( std::ranges::begin( rg ), std::ranges::end( rg ) );
    }
#endif

    iterator erase( const_iterator pos )
    {
      assert( occupied_ > 0 );
      assert( pos != cend() );
      const auto next = std::next( pos );
      const auto full = pos.block_->occupied == pos.block_->capacity;
      // Only one element will be deleted.
      // Manually write down the deletion logic to achieve the most simplified calculation.
      destroy( std::addressof( pos.mut() ) );
      --occupied_;
      --pos.block_->occupied;
      if ( pos.block_->occupied == 0 ) {
        assert( full == false );
        pos.block_->first_hole = Index::npos;
        const auto block       = pos.block_->next;
        remove_hollow( hollow_, pos.block_ );
        if ( pos.block_ != head_ ) {
          unlink( head_, pos.block_ );
          attach( vacuum_, pos.block_ );
        } else
          attach( vacuum_, detach( head_ ) );
        if ( block != nullptr )
          return iterator::from( next );
        return end();
      } else {
        add_hole( pos.block_, pos.offset(), 1 );
        if ( full )
          add_hollow( hollow_, pos.block_ );
      }
      return iterator::from( next );
    }
    iterator erase( const_iterator first, const_iterator last )
    {
      while ( first != last ) {
        if ( first.block_ != last.block_ ) {
          assert( first.block_->next != nullptr );
          const auto full = first.block_->occupied == first.block_->capacity;
          occupied_ -= remove( first );
          if ( first.block_->occupied == 0 ) {
            if ( !full )
              remove_hollow( hollow_, first.block_ );
            if ( first.block_ != head_ ) {
              auto block = first.block_;
              first      = const_iterator::start_of( first.block_->next );
              unlink( head_, block );
              attach( vacuum_, block );
            } else {
              first = const_iterator::start_of( head_->next );
              attach( vacuum_, detach( head_ ) );
              assert( head_ != nullptr ); // due to `last`
            }
          } else {
            if ( full )
              add_hollow( hollow_, first.block_ );
            first = const_iterator::start_of( first.block_->next );
          }
          continue;
        }

        const auto full = first.block_->occupied == first.block_->capacity;
        occupied_ -= remove( first, last );
        if ( first.block_->occupied == 0 ) {
          if ( !full )
            remove_hollow( hollow_, first.block_ );
          // Only the sentinel of the last block can be obtained through the `end` method.
          assert( last == cend() );
          assert( first.block_->next == nullptr );
          if ( first.block_ != head_ )
            unlink( head_, first.block_ );
          else
            detach( head_ );
          attach( vacuum_, first.block_ );
          return end();
        } else if ( full )
          add_hollow( hollow_, first.block_ );
        return iterator::from( last );
      }
      return iterator::from( first );
    }

    void reserve( size_type new_cap )
    {
      if ( new_cap <= capacity_ )
        return;
      throw_if_exceed( new_cap );
      new_cap -= capacity_;

      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      const auto [blanks, total_capacity] =
        reserve_for( block_alloc, area_alloc, new_cap, { min_capacity_, max_capacity_ } );
      concat( vacuum_, blanks );
      capacity_ += total_capacity;
    }

    void trim_capacity()
    {
      if ( vacuum_ == nullptr )
        return;
      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      capacity_ -= discard( block_alloc, area_alloc, vacuum_ );
    }
    void trim_capacity( size_type new_cap )
    { // NOTE: The standard requires complexity linear in the number of blocks deallocated.
      // This implementation follows the same strategy as plf::hive,
      // and may therefore scan the entire reserved chain.
      if ( new_cap >= capacity_ || vacuum_ == nullptr )
        return;
      auto redundant = capacity_ - new_cap;
      if ( redundant < min_capacity_ )
        return;

      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      for ( auto idle = vacuum_->next; idle != nullptr; ) {
        if ( idle->capacity > redundant ) {
          idle = idle->next;
          continue;
        }
        capacity_ -= idle->capacity;
        redundant -= idle->capacity;
        const auto next = idle->next;
        unlink( vacuum_, idle );
        deallocate( block_alloc, area_alloc, std::exchange( idle, next ) );
      }
      if ( vacuum_->capacity <= redundant ) {
        capacity_ -= vacuum_->capacity;
        deallocate( block_alloc, area_alloc, detach( vacuum_ ) );
      }
    }

    void shrink_to_fit()
    {
      if ( occupied_ == capacity_ )
        return;
      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      if ( vacuum_ != nullptr )
        capacity_ -= discard( block_alloc, area_alloc, vacuum_ );
      if ( occupied_ == capacity_ )
        return;

      assert( occupied_ > 0 );
      assert( head_ != nullptr );
      auto target = hollow_;
      if ( auto source = target != nullptr ? target->next_hollow : nullptr; source != nullptr ) {
        assert( head_->next != nullptr );
        auto hole      = Muncher::from( target );
        auto extractor = iterator::start_of( source );
        // There are at least 2 nodes on the chain.
        while ( true ) {
          transfer( hole, extractor );
          // In this point, there are only three possibilities:
          // * the hole is full,
          // * the extractor is exhausted,
          // * or we have reached the end of the hollow chain (In this case, both of the above
          //   situations can occur separately.).
          if ( extractor.exhausted() ) {
            // source is now empty, deallocate it
            const auto next_hollow = source->next_hollow;
            purge( iterator::start_of( source ) );
            remove_hollow( hollow_, source );
            if ( source != head_ )
              unlink( head_, source );
            else
              detach( head_ );
            capacity_ -= source->capacity;
            deallocate( block_alloc, area_alloc, std::exchange( source, next_hollow ) );
            if ( source != nullptr )
              extractor = iterator::start_of( source );
          } else {
            wipe_hole( source, 0, extractor.offset() );
            source->occupied -= purge( iterator::start_of( source ), extractor );
            assert( source->occupied > 0 );
            add_suffix_hole( source, 0, extractor.offset() );
          }
          if ( target->occupied == target->capacity ) {
            // target is now full, let source be the new target
            remove_hollow( hollow_, target );
            target = source;
            if ( target != nullptr ) {
              source = source->next_hollow;
              if ( source == nullptr )
                break;
              hole      = Muncher::from( target );
              extractor = iterator::start_of( source );
            } else
              break;
          } else if ( source == nullptr )
            break;
        }
      }

      assert( target == nullptr || hollow_ == target );
      if ( target != nullptr && target->capacity > min_capacity_ ) {
        assert( target->next_hollow == nullptr );
        assert( target->occupied < target->capacity );
        const auto blank = allocate( block_alloc, area_alloc, (std::max)( target->occupied, min_capacity_ ) );
        auto hole        = Filler::from( blank );
        details::utils::attempt( [&] { transfer( hole, iterator::start_of( target ) ); },
                                 [&] { deallocate( block_alloc, area_alloc, blank ); } );
        if ( blank->occupied < blank->capacity ) {
          build_hole( blank, hole.offset(), hole.rest() );
          hollow_ = blank;
        } else
          hollow_ = nullptr;
        capacity_ -= target->capacity;
        capacity_ += blank->capacity;
        if ( target != head_ ) {
          unlink( head_, target );
          link( head_, blank );
        } else {
          detach( head_ );
          attach( head_, blank );
        }
        purge( iterator::start_of( target ) );
        deallocate( block_alloc, area_alloc, target );
      }
    }

    void reshape( HiveLimits block_limits )
    {
      if ( !is_within_hard_limits( block_limits ) )
#ifdef __cpp_exceptions
        throw std::invalid_argument( "supplied block limits does not satisfy the hard limits" );
#else
        std::terminate();
#endif

      if ( min_capacity_ >= block_limits.min && max_capacity_ <= block_limits.max ) {
        max_capacity_  = block_limits.max;
        min_capacity_  = block_limits.min;
        next_capacity_ = std::clamp( next_capacity_, min_capacity_, max_capacity_ );
        return;
      }

      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      if ( min_capacity_ > block_limits.max || max_capacity_ < block_limits.min ) {
        max_capacity_  = block_limits.max;
        min_capacity_  = block_limits.min;
        next_capacity_ = std::clamp( next_capacity_, min_capacity_, max_capacity_ );
        if ( vacuum_ != nullptr )
          capacity_ -= discard( block_alloc, area_alloc, vacuum_ );
        if ( occupied_ == 0 || capacity_ == 0 )
          return;
        assert( head_ != nullptr );
        auto [list, total_capacity] =
          rebuild_from( block_alloc, area_alloc, begin(), occupied_, block_limits );
        using std::swap;
        swap( head_, list );
        hollow_ = nullptr;
        if ( head_->occupied < head_->capacity )
          add_hollow( hollow_, head_ );
        capacity_ = total_capacity;
        eliminate( block_alloc, area_alloc, list );
        return;
      }

      // If an exception is thrown, the specified block limit will violate the current hive limit.
      // This is what is called an "unspecified result".
      max_capacity_  = block_limits.max;
      min_capacity_  = block_limits.min;
      next_capacity_ = std::clamp( next_capacity_, min_capacity_, max_capacity_ );
      if ( vacuum_ != nullptr )
        capacity_ -= discard_unfit( block_alloc, area_alloc, vacuum_, block_limits );
      if ( occupied_ == 0 || capacity_ == 0 )
        return;

      Unit unfitness      = nullptr;
      size_type num_unfit = 0;
      // collect the blocks that does not satisfy the new limit
      for ( auto block = head_->next; block != nullptr; ) {
        if ( block->capacity >= min_capacity_ && block->capacity <= max_capacity_ ) {
          block = block->next;
          continue;
        }
        num_unfit += block->occupied;
        if ( block->occupied < block->capacity )
          remove_hollow( hollow_, block );
        const auto next = block->next;
        unlink( head_, block );
        attach( unfitness, block );
        block = next;
      }
      if ( head_->capacity < min_capacity_ || head_->capacity > max_capacity_ ) {
        num_unfit += head_->occupied;
        if ( head_->occupied < head_->capacity )
          remove_hollow( hollow_, head_ );
        attach( unfitness, detach( head_ ) );
      }
      if ( unfitness == nullptr )
        return;

      auto extractor = iterator::start_of( unfitness );
      details::utils::attempt(
        [&] {
          if ( head_ == nullptr ) {
            auto [list, new_capacity] =
              rebuild_from( block_alloc, area_alloc, extractor, num_unfit, block_limits );
            if ( list->occupied < list->capacity )
              add_hollow( hollow_, list );
            head_ = list;
            capacity_ += new_capacity;
            return;
          }

          if ( vacuum_ != nullptr ) {
            auto [list, num_transferred] = migrate( vacuum_, extractor );
            if ( num_transferred == num_unfit && list->occupied < list->capacity )
              add_hollow( hollow_, list );
            concat( head_, list );
            num_unfit -= num_transferred;
          }
          if ( !extractor.exhausted() ) {
            const auto [blanks, new_capacity] =
              reserve_for( block_alloc, area_alloc, num_unfit, block_limits );
            capacity_ += new_capacity;
            concat( vacuum_, blanks );

            auto [list, num_transferred] = migrate( vacuum_, extractor );
            assert( num_transferred == num_unfit );
            if ( list->occupied < list->capacity )
              add_hollow( hollow_, list );
            concat( head_, list );
          }
        },
        [&] {
          // Exception will only be thrown during `transfer_to`, the unfitness must be not null.
          assert( unfitness != nullptr );
          while ( unfitness != extractor.block_ ) {
            purge( iterator::start_of( unfitness ) );
            deallocate( block_alloc, area_alloc, detach( unfitness ) );
          }
          // Similarly, since the transfer process has not been completed,
          // it is certain that the unfitness has not been fully accessed.
          if ( extractor != iterator::start_of( unfitness ) ) {
            wipe_hole( unfitness, 0, extractor.offset() );
            unfitness->occupied -= purge( iterator::start_of( unfitness ), extractor );
            add_suffix_hole( unfitness, 0, extractor.offset() );
          }

          do {
            if ( unfitness->occupied < unfitness->capacity )
              add_hollow( hollow_, unfitness );
            attach( head_, detach( unfitness ) );
          } while ( unfitness != nullptr );
        } );

      eliminate( block_alloc, area_alloc, unfitness );
    }

    void clear() noexcept
    {
      while ( head_ != nullptr ) {
        remove( head_ );
        head_->prev_hollow = head_->next_hollow = nullptr;
        attach( vacuum_, std::exchange( head_, head_->next ) );
      }
      occupied_ = 0;
      hollow_   = nullptr;
      assert( head_ == nullptr );
    }

    void splice( Hive& other )
    {
      assert( this != &other );
      assert( this->allocator() == other.allocator() );

      if ( other.size() == 0 )
        return;
      throw_if_violate( other.min_capacity_, other.max_capacity_ );
      size_type transferred_cap = 0;

      BiList sparse { other.hollow_, other.hollow_ };
      for ( auto block = other.head_; block != nullptr; block = block->next ) {
        throw_if_violate( block->capacity, block->capacity );
        transferred_cap += block->capacity;
        if ( block->next_hollow == nullptr && block->occupied < block->capacity )
          sparse.tail = block;
      }

      if ( other.hollow_ != nullptr )
        splice_hollow( sparse, hollow_ );
      concat( head_, other.head_ );
      other.head_     = nullptr;
      other.hollow_   = nullptr;
      other.occupied_ = 0;
      other.capacity_ -= transferred_cap;
      capacity_ += transferred_cap;
    }
    void splice( Hive&& other ) { splice( other ); }

    template<typename Pred = std::equal_to<Tp>>
    size_type unique( Pred pred = Pred() )
    {
      if ( occupied_ < 2 )
        return 0;
      size_type total_repetition   = 0;
      size_type currently_repeated = 0;
      auto first                   = begin();
      auto cursor                  = std::next( first );
      while ( true ) {
        // block-based iteration
        const bool repetitive   = std::invoke( pred, *first, *cursor );
        // The most recently identified element, not the last-tail element.
        auto last               = cursor;
        const bool within_block = cursor.advance();
        currently_repeated += static_cast<size_type>( repetitive );
        if ( !repetitive || !within_block ) {
          if ( currently_repeated == 0 ) {
            assert( repetitive == false );
            if ( within_block || cursor.next() ) {
              first = last;
              continue;
            }
            break;
          }

          const auto no_more_block = !within_block && !cursor.next();
          const auto full          = first.block_->occupied == first.block_->capacity;
          if ( last.block_ == first.block_ ) {
            // remove suffix
            const auto iter  = std::next( first );
            Skipfield length = 0;
            if ( repetitive && !within_block ) {
              // remove [first + 1, last + 1)
              wipe( iter );
              length = static_cast<Skipfield>( iter.block_->capacity - iter.offset() );
            } else if ( repetitive ) {
              wipe( iter, std::next( last ) );
              length = last.offset() - iter.offset() + 1;
            } else {
              // remove [first + 1, last)
              wipe( iter, last );
              length = last.offset() - iter.offset();
              first  = last;
            }
            // The `first` is valid, therefore,
            // there will definitely be no forward merger opportunity for the new hole.
            add_suffix_hole( iter.block_, iter.offset(), length );
            if ( full )
              add_hollow( hollow_, first.block_ );
            assert( last.block_->occupied > 0 );
          } else if ( repetitive && !within_block ) {
            // remove the whole block pointed to by `last`.
            remove( last.block_ );
            assert( last.block_->occupied == 0 );
            // The last.block_ is impossible to be the head_ because of `first`.
            assert( last.block_ != head_ );
            if ( !full )
              remove_hollow( hollow_, last.block_ );
            unlink( head_, last.block_ );
            attach( vacuum_, last.block_ );
          } else {
            // remove prefix
            assert( repetitive == false );
            wipe_hole( last.block_, 0, last.offset() );
            last.block_->occupied -= purge( iterator::start_of( last.block_ ), last );
            add_suffix_hole( last.block_, 0, last.offset() );
            assert( last.block_->occupied > 0 );
            if ( full )
              add_hollow( hollow_, first.block_ );
            first = last;
          }
          occupied_ -= currently_repeated;
          total_repetition += currently_repeated;
          if ( !within_block && no_more_block )
            break;
          currently_repeated = 0;
        }
      }
      return total_repetition;
    }

    template<typename Comp = std::less<Tp>>
    void sort( Comp comp = Comp() )
    {
      if ( occupied_ < 2 )
        return;
      if constexpr ( sizeof( Tp ) <= 2 * sizeof( void* )
                     && (std::is_trivially_copyable_v<Tp> || std::is_move_assignable_v<Tp>)) {
        // Since Tp is a sub-object of Payload,
        // the element pointed to by the free memory block must meet the alignment requirements of Tp;
        // and if there is such a free block with the size that meets our needs, we will reuse its memory.
        static_assert( alignof( Tp ) <= alignof( Payload ) );
        Unit borrowed = borrow_from( vacuum_, occupied_ * sizeof( Tp ) );
        if ( borrowed != nullptr )
          direct_sort( details::utils::pointer_cast<Tp*>( borrowed->element ), std::move( comp ) );
        else {
          const auto allocated = std::allocator_traits<Alloc>::allocate( this->allocator(), occupied_ );
          details::utils::ensure(
            [&] { direct_sort( std::to_address( allocated ), std::move( comp ) ); },
            [&] { std::allocator_traits<Alloc>::deallocate( this->allocator(), allocated, occupied_ ); } );
        }
      } else {
        if constexpr ( alignof( Tp* ) <= Layout::alignment && alignof( size_type ) <= Layout::alignment ) {
          Unit addresses = borrow_from( vacuum_, occupied_ * sizeof( Tp* ) );
          Unit mapping   = borrow_from( vacuum_, occupied_ * sizeof( size_type ) );
          if ( mapping != nullptr && mapping == addresses )
            mapping = borrow_from( mapping->next, occupied_ * sizeof( size_type ) );
          if ( addresses != nullptr && mapping != nullptr ) {
            indirect_sort( details::utils::pointer_cast<Tp**>( addresses->element ),
                           details::utils::pointer_cast<size_type*>( mapping->element ),
                           std::move( comp ) );
            return;
          }
        }
        using BufferLayout = details::utils::SoALayout<Tp*, size_type>;
        if constexpr ( BufferLayout::alignment <= Layout::alignment ) {
          // If we cannot find two small memory blocks but can find one sufficiently large, use it.
          Unit borrowed = borrow_from( vacuum_, BufferLayout::allocation( occupied_ ) );
          if ( borrowed != nullptr ) {
            indirect_sort( details::utils::pointer_cast<Tp**>( borrowed->element ),
                           details::utils::pointer_cast<size_type*>(
                             borrowed->element + BufferLayout::split( occupied_ ).front() ),
                           std::move( comp ) );
            return;
          }
        }
        using BufferAlloc = BufferLayout::template rebind_alloc<Alloc>;
        BufferAlloc buffer_alloc { this->allocator() };
        const auto allocated = BufferLayout::allocate( buffer_alloc, occupied_ );
        details::utils::ensure(
          [&] {
            indirect_sort( details::utils::pointer_cast<Tp**>( allocated ),
                           reinterpret_cast<size_type*>( details::utils::pointer_cast<std::byte*>( allocated )
                                                         + BufferLayout::split( occupied_ ).front() ),
                           std::move( comp ) );
          },
          [&] { BufferLayout::deallocate( buffer_alloc, allocated, occupied_ ); } );
      }
    }

    void swap( Hive& other )
      noexcept( std::allocator_traits<allocator_type>::propagate_on_container_swap::value
                || std::allocator_traits<allocator_type>::is_always_equal::value )
    {
      this->pocs( other.allocator() );
      interchange( other );
    }
    friend void swap( Hive& a, Hive& b ) noexcept( noexcept( a.swap( b ) ) ) { a.swap( b ); }

    // Please pay attention to the ODR-use issue when defining the macro NEST_DEBUG.
#ifdef NEST_DEBUG
    /// @brief This method is intended SOLELY for DEBUG purposes and SHOULD NOT APPEAR IN NORMAL CODE.
    ///        Element format: `["string"]` (has data) or `{count;prev,next}` (just hole)
    ///        Skip format:    `[0;length]` (repeated), `0`, `1` (separately)
    ///                        or `{length;...;length}` (just hole)
    template<typename OutputIter>
    [[nodiscard]] OutputIter _dump_to( OutputIter oiter ) const
    { // DEBUG ONLY
      size_type id = 0;
      auto block   = head_;
      while ( true ) {
        oiter = std::format_to( oiter,
                                "Block #{}  [capacity={}, occupied={}",
                                id++,
                                block->capacity,
                                block->occupied );
        if ( block->first_hole != Index::npos )
          oiter = std::format_to( oiter, ", first_hole={}]", block->first_hole );
        else
          *( oiter++ ) = ']';

        oiter          = std::ranges::copy( "\n Element: ", oiter ).out;
        auto element   = block->element;
        auto skipfield = block->skipfield;
        while ( true ) {
          if ( as_skipfield( skipfield ) > 0 ) {
            element          = address_at<Payload>( element, as_skipfield( skipfield ) - 1 );
            const auto index = as_index( element );
            oiter            = std::format_to( oiter, "{{{};", as_skipfield( skipfield ) );
            if ( index.prev == index.next ) {
              assert( index.prev == Index::npos );
              oiter = std::ranges::copy( "#,#", oiter ).out;
            } else if ( index.prev == Index::npos )
              oiter = std::format_to( oiter, "#,{}", as_index( element ).next );
            else if ( index.next == Index::npos )
              oiter = std::format_to( oiter, "{},#", as_index( element ).prev );
            else
              oiter = std::format_to( oiter, "{},{}", as_index( element ).prev, as_index( element ).next );
            *( oiter++ ) = '}';
            element      = address_at<Payload>( element, 1 );
            skipfield    = address_at<Skipfield>( skipfield, as_skipfield( skipfield ) );
          } else {
            oiter     = std::format_to( oiter, "[{}]", as_data( element ) );
            element   = address_at<Payload>( element, 1 );
            skipfield = address_at<Skipfield>( skipfield, 1 );
          }
          if ( element < block->skipfield )
            oiter = std::ranges::copy( ", ", oiter ).out;
          else
            break;
        }

        oiter                     = std::ranges::copy( "\n  Skip:    ", oiter ).out;
        skipfield                 = block->skipfield;
        Skipfield continuous_zero = 0;
        while ( true ) {
          if ( as_skipfield( skipfield ) > 0 ) {
            if ( continuous_zero > 1 )
              oiter = std::format_to( oiter, "[0;{}], ", continuous_zero );
            else
              oiter = std::ranges::copy( "0, ", oiter ).out;
            continuous_zero = 0;
            if ( as_skipfield( skipfield ) > 1 )
              oiter = std::format_to(
                oiter,
                "{{{};...;{}}}",
                as_skipfield( skipfield ),
                as_skipfield( address_at<Skipfield>( skipfield, as_skipfield( skipfield ) - 1 ) ) );
            else
              *( oiter++ ) = '1';
            skipfield = address_at<Skipfield>( skipfield, as_skipfield( skipfield ) );
          } else {
            skipfield = address_at<Skipfield>( skipfield, 1 );
            ++continuous_zero;
          }
          if ( skipfield < address_at<Skipfield>( block->skipfield, block->capacity ) ) {
            if ( continuous_zero == 0 )
              oiter = std::ranges::copy( ", ", oiter ).out;
          } else {
            if ( continuous_zero > 1 )
              oiter = std::format_to( oiter, "[0;{}]", continuous_zero );
            else if ( continuous_zero > 0 )
              *( oiter++ ) = '0';
            break;
          }
        }

        block = block->next;
        if ( block != nullptr )
          *( oiter++ ) = '\n';
        else
          break;
      }
      return oiter;
    }
    [[nodiscard]] std::string _dump() const
    {
      std::string buffer;
      (void)_dump_to( std::back_inserter( buffer ) );
      return buffer;
    }
#endif

  private:
    template<typename... Args>
    static constexpr auto _trivially_constructible_v =
      details::traits::AllocatorTriviallyConstructible<Alloc, Tp, Args...>
      && ( details::traits::AllocatorTriviallyDestructible<Alloc, Tp> || std::is_trivially_copyable_v<Tp>
           || std::is_nothrow_constructible_v<Tp, Args...> );
    static constexpr auto _trivially_destructible =
      details::traits::AllocatorTriviallyDestructible<Alloc, Tp> && std::is_trivially_destructible_v<Tp>;

    // member variables:

    // `head_` is a bidirectional linked list.
    Unit head_               = nullptr;
    // `vacuum_` is a bidirectional linked list.
    Unit vacuum_             = nullptr;
    // `hollow_` is a special bidirectional linked list.
    Unit hollow_             = nullptr;
    size_type occupied_      = 0;
    size_type capacity_      = 0;
    Skipfield max_capacity_  = max_default_capacity();
    Skipfield min_capacity_  = min_default_capacity();
    Skipfield next_capacity_ = min_default_capacity();

    NEST_FORCEINLINE constexpr Skipfield grow( size_type last_capacity ) const noexcept
    { // accept size_type to prevent overflow
      return static_cast<Skipfield>(
        std::clamp<size_type>( last_capacity * 2, min_capacity_, max_capacity_ ) );
    }

    NEST_FORCEINLINE constexpr void throw_if_violate( Skipfield min_cap, Skipfield max_cap ) const
    {
      if ( min_cap > max_capacity_ || max_cap < min_capacity_ ) [[unlikely]]
#ifdef __cpp_exceptions
        throw std::length_error( "the source's active block limits does not satisfy the current limits" );
#else
        std::terminate();
      (void)min_cap;
      (void)max_cap;
#endif
    }
    NEST_FORCEINLINE constexpr void throw_if_exceed( size_type expected ) const
    {
      if ( expected > max_size() ) [[unlikely]]
#ifdef __cpp_exceptions
        throw std::length_error( "new size would exceed max_size()" );
#else
        std::terminate();
      (void)expected;
#endif
    }

    // Append a block from to the front of `list`, this function will not change the hollow pointer.
    NEST_FORCEINLINE static constexpr void attach( Unit& list, Unit block ) noexcept
    {
      assert( block != nullptr );
      if ( list != nullptr ) {
        block->next = list;
        block->prev = list->prev;
        list->prev  = block;
      } else
        block->prev = block;
      list = block;
    }
    // Take a block from the head of list, this function will not change the hollow pointer.
    NEST_FORCEINLINE static constexpr Unit detach( Unit& list ) noexcept
    {
      assert( list != nullptr );
      auto block = list;
      list       = list->next;
      if ( list != nullptr )
        list->prev = block->prev;
      block->prev = block->next = nullptr;
      return block;
    }

    // Append a block from to the front of `list`, the list cannot be empty.
    NEST_FORCEINLINE static constexpr void link( Unit& list, Unit block ) noexcept
    {
      assert( list != nullptr );
      assert( block != nullptr );
      block->next = list;
      block->prev = list->prev;
      list->prev  = block;
      list        = block;
    }
    // Remove a specified block from its list, the block cannot be the head node of the list.
    // This function will not change the hollow pointer.
    NEST_FORCEINLINE static constexpr void unlink( Unit& list, Unit& block ) noexcept
    {
      assert( list != block );
      assert( block != nullptr );
      assert( block->prev != nullptr );
      block->prev->next = block->next;
      if ( block->next != nullptr )
        block->next->prev = block->prev;
      else {
        assert( list->prev == block );
        list->prev = block->prev;
      }
      block->prev = block->next = nullptr;
    }

    // Append the linked list `[front, tail]` to the front of `other`,
    // this function will not change the hollow pointer.
    static constexpr void concat( Unit& list, Unit other ) noexcept
    {
      assert( other != nullptr );
      assert( other->prev != nullptr );
      if ( list != nullptr ) {
        list->prev->next = other;
        using std::swap;
        swap( list->prev, other->prev );
      } else
        list = other;
    }

    // Add a newly hollow block into the hollow linked list.
    NEST_FORCEINLINE static constexpr void add_hollow( Unit& hollow, Unit& block ) noexcept
    {
      assert( block != nullptr );
      assert( block->occupied < block->capacity );
      assert( block->occupied > 0 );
      assert( block->prev_hollow == nullptr );
      assert( block->next_hollow == nullptr );
      assert( hollow != block );
      if ( hollow != nullptr ) {
        // push front
        hollow->prev_hollow = block;
        block->next_hollow  = hollow;
        hollow              = block;
      } else
        hollow = block;
    }
    // Remove the hollow relation from the list.
    NEST_FORCEINLINE static constexpr void remove_hollow( Unit& hollow, Unit& block ) noexcept
    {
      assert( hollow != nullptr );
      assert( block != nullptr );
      // `reshape` will strip an unprocessed block,
      // so it cannot be asserted that a block must be empty or full
      if ( block->prev_hollow != nullptr )
        block->prev_hollow->next_hollow = block->next_hollow;
      else {
        assert( hollow == block );
        hollow = block->next_hollow;
      }
      if ( block->next_hollow != nullptr )
        block->next_hollow->prev_hollow = block->prev_hollow;
      block->prev_hollow = block->next_hollow = nullptr;
    }
    // Splice two hollow chain.
    static constexpr void splice_hollow( BiList hollow, Unit& other ) noexcept
    {
      assert( hollow.head != nullptr );
      assert( hollow.tail != nullptr );
      if ( other != nullptr ) {
        hollow.tail->next_hollow = other;
        other->prev_hollow       = hollow.tail;
      } else
        other = hollow.head;
    }

    /// @brief Find a block meet the specified `expected` number of bytes.
    /// @param expected The expected size, in bytes.
    static constexpr Unit borrow_from( Unit list, size_type expected ) noexcept
    {
      for ( ; list != nullptr; list = list->next ) {
        // we don't borrow the last dummy skipfield
        if ( Layout::allocation( list->capacity ) >= expected )
          return list;
      }
      return nullptr;
    }

    NEST_FORCEINLINE static void remove_hole( Unit block, Index index ) noexcept
    {
      assert( block->first_hole != Index::npos );
      if ( index.prev != Index::npos )
        index_at( block, index.prev ).next = index.next;
      else
        block->first_hole = index.next;
      if ( index.next != Index::npos )
        index_at( block, index.next ).prev = index.prev;
    }
    /// @brief Fix the length information of the hole
    /// @param length The new length.
    NEST_FORCEINLINE static void fix_hole( Area first, Skipfield length ) noexcept
    { as_skipfield( first ) = as_skipfield( address_at<Skipfield>( first, length - 1 ) ) = length; }
    /// @brief Create a hole in the block.
    /// @param pos The position of the first element.
    NEST_FORCEINLINE static void build_hole( Unit block, Skipfield pos, Skipfield length ) noexcept
    {
      assert( pos + length - 1 != Index::npos );
      assert( block->first_hole == Index::npos );
      const auto origin =
        details::utils::pointer_cast<Skipfield*>( address_at<Skipfield>( block->skipfield, pos ) );
      std::construct_at( origin, length );
      std::construct_at( origin + length - 1, length );
      // The index is only present in the last element.
      std::construct_at( as_address<Payload>( block->element, pos + length - 1 ),
                         Index { Index::npos, Index::npos } );
      block->first_hole = pos + length - 1;
    }
    /// @brief Add a new hole to the block.
    /// @param pos The position of the first element.
    NEST_FORCEINLINE static void add_hole( Unit block, Skipfield pos, Skipfield length ) noexcept
    {
      assert( pos + length - 1 != Index::npos );
      if ( block->first_hole != Index::npos ) {
        // Pointers that are out of range but will not be dereferenced are safe.
        const auto prev_neighbor = address_at<Skipfield>( block->skipfield, pos - 1 );
        // Note that allocate allocates an extra element at the end of the Skipfield array,
        // so dereferencing this pointer is always safe.
        const auto next_neighbor = address_at<Skipfield>( block->skipfield, pos + length );
        if ( pos > 0 && as_skipfield( prev_neighbor ) > 0 && as_skipfield( next_neighbor ) > 0 ) {
          // merge both
          skipfield_at( block, pos - as_skipfield( prev_neighbor ) ) =
            skipfield_at( block, pos + length + as_skipfield( next_neighbor ) - 1 ) =
              as_skipfield( prev_neighbor ) + as_skipfield( next_neighbor ) + length;
          // erase left
          remove_hole( block, index_at( block, pos - 1 ) );
        } else if ( pos > 0 && as_skipfield( prev_neighbor ) > 0 ) {
          // merge left
          skipfield_at( block, pos - as_skipfield( prev_neighbor ) ) =
            skipfield_at( block, pos + length - 1 ) = as_skipfield( prev_neighbor ) + length;
          // update left
          const auto index                          = index_at( block, pos - 1 );
          std::construct_at( as_address<Payload>( block->element, pos + length - 1 ), index );
          if ( index.prev != Index::npos )
            index_at( block, index.prev ).next = pos + length - 1;
          else {
            assert( block->first_hole == pos - 1 );
            block->first_hole = pos + length - 1;
          }
          if ( index.next != Index::npos )
            index_at( block, index.next ).prev = pos;
        } else if ( as_skipfield( next_neighbor ) > 0 ) {
          // merge right
          skipfield_at( block, pos ) =
            skipfield_at( block, pos + length + as_skipfield( next_neighbor ) - 1 ) =
              as_skipfield( next_neighbor ) + length;
        } else {
          // create a new hole
          as_skipfield( prev_neighbor + sizeof( Skipfield ) ) =
            as_skipfield( next_neighbor - sizeof( Skipfield ) ) = length;
          std::construct_at( as_address<Payload>( block->element, pos + length - 1 ),
                             Index { Index::npos, block->first_hole } );
          index_at( block, block->first_hole ).prev = pos + length - 1;
          block->first_hole                         = pos + length - 1;
        }
      } else
        build_hole( block, pos, length );
    }
    /// @brief Add a new hole that does not have any backward holes.
    NEST_FORCEINLINE static void add_prefix_hole( Unit block, Skipfield pos, Skipfield length ) noexcept
    {
      assert( pos + length - 1 != Index::npos );
      if ( block->first_hole != Index::npos ) {
        const auto prev_neighbor = address_at<Skipfield>( block->skipfield, pos - 1 );
        if ( pos > 0 && as_skipfield( prev_neighbor ) > 0 ) {
          // merge left
          skipfield_at( block, pos - as_skipfield( prev_neighbor ) ) =
            skipfield_at( block, pos + length - 1 ) = as_skipfield( prev_neighbor ) + length;
          // update left
          const auto index                          = index_at( block, pos - 1 );
          std::construct_at( as_address<Payload>( block->element, pos + length - 1 ), index );
          if ( index.prev != Index::npos )
            index_at( block, index.prev ).next = pos + length - 1;
          else {
            assert( block->first_hole == pos - 1 );
            block->first_hole = pos + length - 1;
          }
          if ( index.next != Index::npos )
            index_at( block, index.next ).prev = pos + length - 1;
        } else {
          // create a new hole
          as_skipfield( prev_neighbor + sizeof( Skipfield ) ) = skipfield_at( block, pos + length - 1 ) =
            length;
          std::construct_at( as_address<Payload>( block->element, pos + length - 1 ),
                             Index { Index::npos, block->first_hole } );
          index_at( block, block->first_hole ).prev = pos + length - 1;
          block->first_hole                         = pos + length - 1;
        }
      } else
        build_hole( block, pos, length );
    }
    /// @brief Add a new hole that does not have any forward holes.
    NEST_FORCEINLINE static void add_suffix_hole( Unit block, Skipfield pos, Skipfield length ) noexcept
    {
      assert( pos + length - 1 != Index::npos );
      if ( block->first_hole != Index::npos ) {
        const auto next_neighbor = address_at<Skipfield>( block->skipfield, pos + length );
        if ( as_skipfield( next_neighbor ) > 0 ) {
          // merge right
          skipfield_at( block, pos ) =
            skipfield_at( block, pos + length + as_skipfield( next_neighbor ) - 1 ) =
              as_skipfield( next_neighbor ) + length;
        } else {
          // create a new hole
          skipfield_at( block, pos ) = as_skipfield( next_neighbor - sizeof( Skipfield ) ) = length;
          std::construct_at( as_address<Payload>( block->element, pos + length - 1 ),
                             Index { Index::npos, block->first_hole } );
          index_at( block, block->first_hole ).prev = pos + length - 1;
          block->first_hole                         = pos + length - 1;
        }
      } else
        build_hole( block, pos, length );
    }

    static constexpr Unit allocate( BlockAlloc& block_alloc, AreaAlloc& area_alloc, size_type cap )
    {
      const auto scheme  = typename Layout::scheme_type { cap, cap + 1u };
      const auto element = Layout::allocate( area_alloc, scheme );
      return details::utils::attempt(
        [&] {
          auto block = std::allocator_traits<BlockAlloc>::allocate( block_alloc, 1 );
          std::allocator_traits<BlockAlloc>::construct( block_alloc, std::to_address( block ) );
          block->element   = details::utils::pointer_cast<Area>( element );
          block->skipfield = block->element + Layout::split( scheme ).front();
          // Since the Payload contains a Gap type aligned with the Skipfield,
          // padding is always zero under any circumstances.
          assert( Layout::split( scheme ).front() == cap * sizeof( Payload ) );
          // We want to always increment element_ unconditionally when the Iterator is incremented,
          // and then check for out-of-bounds,
          // which saves one conditional branch compared to the normal approach.
          // To support this operation without causing undefined behavior,
          // we must allocate an additional dummy element at the end of the skipfield array.
          std::construct_at( details::utils::pointer_cast<Skipfield*>( block->skipfield ) + cap, 0 );
          block->capacity = static_cast<Skipfield>( cap );
          return block;
        },
        [&] { Layout::deallocate( area_alloc, element, cap ); } );
    }
    static constexpr void deallocate( BlockAlloc& block_alloc, AreaAlloc& area_alloc, Unit block ) noexcept
    {
      Layout::deallocate( area_alloc, block->element, { block->capacity, block->capacity + 1u } );
      std::allocator_traits<BlockAlloc>::destroy( block_alloc, std::to_address( block ) );
      std::allocator_traits<BlockAlloc>::deallocate( block_alloc, block, 1 );
    }

    // Discard all the blocks in the idle list.
    static constexpr size_type discard( BlockAlloc& block_alloc, AreaAlloc& area_alloc, Unit& idle ) noexcept
    {
      assert( idle != nullptr );
      size_type discarded_cap = 0;
      do {
        assert( idle->occupied == 0 );
        discarded_cap += idle->capacity;
        deallocate( block_alloc, area_alloc, std::exchange( idle, idle->next ) );
      } while ( idle != nullptr );
      assert( idle == nullptr );
      return discarded_cap;
    }
    // Discard the blocks in the idle list that do not meet the limits restrictions.
    static constexpr size_type discard_unfit( BlockAlloc& block_alloc,
                                              AreaAlloc& area_alloc,
                                              Unit& idle,
                                              HiveLimits limits ) noexcept
    {
      assert( idle != nullptr );
      size_type discarded_cap = 0;

      for ( auto current = idle->next; current != nullptr; ) {
        if ( current->capacity < limits.min || current->capacity > limits.max ) {
          const auto next = idle->next;
          unlink( idle, current );
          discarded_cap += current->capacity;
          deallocate( block_alloc, area_alloc, std::exchange( current, next ) );
        } else
          current = current->next;
      }
      if ( idle->capacity < limits.min || idle->capacity > limits.max ) {
        discarded_cap += idle->capacity;
        deallocate( block_alloc, area_alloc, detach( idle ) );
      }
      return discarded_cap;
    }

    /// @return (List, real_capacity)
    static constexpr std::pair<Unit, size_type> reserve_for( BlockAlloc& block_alloc,
                                                             AreaAlloc& area_alloc,
                                                             size_type capacity,
                                                             HiveLimits limits )
    {
      assert( capacity > 0 );
      const auto min_quantity = ( capacity + limits.max - 1 ) / limits.max;
      assert( min_quantity > 0 );
      const auto average_cap = capacity / min_quantity;
      assert( average_cap <= limits.max );

      Unit list = nullptr;
      if ( average_cap < limits.min ) {
        attach( list, allocate( block_alloc, area_alloc, limits.min ) );
        details::utils::attempt(
          [&] {
            for ( size_type i = 1; i < min_quantity; ++i )
              link( list, allocate( block_alloc, area_alloc, limits.min ) );
          },
          [&] { discard( block_alloc, area_alloc, list ); } );
        return { list, min_quantity * limits.min };
      }

      const auto num_large  = capacity % min_quantity;
      const auto num_normal = min_quantity - num_large;

      details::utils::attempt(
        [&] {
          if ( num_large > 0 ) {
            attach( list, allocate( block_alloc, area_alloc, average_cap + 1 ) );
            for ( size_type i = 1; i < num_large; ++i )
              link( list, allocate( block_alloc, area_alloc, average_cap + 1 ) );
          }
          if ( num_normal > 0 ) {
            if ( list == nullptr )
              attach( list, allocate( block_alloc, area_alloc, average_cap ) );
            else
              link( list, allocate( block_alloc, area_alloc, average_cap ) );
            for ( size_type i = 1; i < num_normal; ++i )
              link( list, allocate( block_alloc, area_alloc, average_cap ) );
          }
        },
        [&] {
          if ( list != nullptr )
            discard( block_alloc, area_alloc, list );
        } );
      return { list, num_normal * average_cap + num_large * ( average_cap + 1 ) };
    }

    NEST_FORCEINLINE constexpr void destroy( Tp* element ) noexcept
    {
      if constexpr ( !_trivially_destructible )
        std::allocator_traits<Alloc>::destroy( this->allocator(), element );
    }

    // Destroy the valid elements within a continuous range.
    NEST_FORCEINLINE constexpr void purge( Accessor first, Accessor last ) noexcept
    {
      assert( first < last );
      if constexpr ( !_trivially_destructible )
        do
          destroy( std::addressof( *( first++ ) ) );
        while ( first != last );
    }
    NEST_FORCEINLINE constexpr void purge( Tp* first, const Tp* last ) noexcept
    {
      assert( first < last );
      if constexpr ( !_trivially_destructible )
        do
          destroy( first++ );
        while ( first != last );
    }

    // Destroy all elements on the block starting from iter itself.
    // This function does nothing but destroy elements.
    NEST_FORCEINLINE Skipfield purge( iterator iter ) noexcept
    {
      assert( iter != iterator() );
      assert( iter.exhausted() == false );
      assert( iter.block_->occupied <= iter.block_->capacity );
      if ( iter.element_ == iter.block_->element
           && ( iter.block_->occupied == iter.block_->capacity
                || iter.block_->first_hole == iter.block_->occupied ) ) {
        purge( Accessor::from_object( iter.block_->element ),
               Accessor::from_address( address_at<Payload>( iter.block_->element, iter.block_->occupied ) ) );
        return iter.block_->occupied;
      }
      Skipfield num_dropped = 0;
      do {
        if constexpr ( !_trivially_destructible )
          std::allocator_traits<Alloc>::destroy( this->allocator(), std::addressof( *iter ) );
        ++num_dropped;
      } while ( iter.advance() );
      return num_dropped;
    }
    NEST_FORCEINLINE Skipfield purge( iterator first, iterator last ) noexcept
    {
      assert( first != last );
      assert( last.exhausted() == false );
      Skipfield num_dropped = 0;
      do {
        if constexpr ( !_trivially_destructible )
          std::allocator_traits<Alloc>::destroy( this->allocator(), std::addressof( *first ) );
        ++num_dropped;
      } while ( ++first != last );
      return num_dropped;
    }

    // Wipe the holes between [first, last).
    void wipe_hole( Unit block, Skipfield first, Skipfield last ) noexcept
    {
      assert( block != nullptr );
      assert( first != Index::npos );
      assert( last != Index::npos );
      assert( first < last );
      assert( last <= block->capacity );
      if ( block->first_hole == Index::npos )
        return;
      auto element        = address_at<Payload>( block->element, first );
      auto skipfield      = address_at<Skipfield>( block->skipfield, first );
      const auto terminus = address_at<Skipfield>( block->skipfield, last );
      do {
        if ( as_skipfield( skipfield ) > 0 ) {
          remove_hole( block, as_index( element ) );
          element   = address_at<Payload>( element, as_skipfield( skipfield ) );
          skipfield = address_at<Skipfield>( skipfield, as_skipfield( skipfield ) );
        } else {
          element   = address_at<Payload>( element, 1 );
          skipfield = address_at<Skipfield>( skipfield, 1 );
        }
      } while ( skipfield < terminus );
    }

    // Destroy all elements on the block starting from `first` itself.
    // This function will remove the hole when iterating, but will not recover the hole info.
    Skipfield wipe( const_iterator first ) noexcept
    {
      assert( first != const_iterator() );
      assert( first.exhausted() == false );
      destroy( std::addressof( first.mut() ) );
      Skipfield num_deleted = 1;
      // Manually advance the iteration, so that there will be one less skipfield read operation.
      // In fact, it can be implemented as "advance + peek previous skipfield".
      for ( auto iter = first;; ) {
        iter.skipfield_ = address_at<Skipfield>( iter.skipfield_, 1 );
        if ( as_skipfield( iter.skipfield_ ) > 0 ) {
          iter.element_ = address_at<Payload>( iter.element_, as_skipfield( iter.skipfield_ ) );
          remove_hole( iter.block_, as_index( iter.element_ ) );
          iter.skipfield_ = address_at<Skipfield>( iter.skipfield_, as_skipfield( iter.skipfield_ ) );
        }
        iter.element_ = address_at<Payload>( iter.element_, 1 );
        if ( iter.exhausted() )
          break;
        destroy( std::addressof( iter.mut() ) );
        ++num_deleted;
      }

      first.block_->occupied -= num_deleted;
      return num_deleted;
    }
    // Destroy all valid elements within [first, last), the iterator must point to a same block.
    // This function will remove the hole when iterating, but will not recover the hole info.
    Skipfield wipe( const_iterator first, const_iterator last ) noexcept
    {
      assert( first != last );
      assert( first.block_ == last.block_ );
      destroy( std::addressof( first.mut() ) );
      Skipfield num_deleted = 1;
      for ( auto iter = first;; ) {
        iter.skipfield_ = address_at<Skipfield>( iter.skipfield_, 1 );
        if ( as_skipfield( iter.skipfield_ ) > 0 ) {
          iter.element_ = address_at<Payload>( iter.element_, as_skipfield( iter.skipfield_ ) );
          remove_hole( iter.block_, as_index( iter.element_ ) );
          iter.skipfield_ = address_at<Skipfield>( iter.skipfield_, as_skipfield( iter.skipfield_ ) );
        }
        iter.element_ = address_at<Payload>( iter.element_, 1 );
        if ( iter == last )
          break;
        destroy( std::addressof( iter.mut() ) );
        ++num_deleted;
      }
      first.block_->occupied -= num_deleted;
      return num_deleted;
    }

    // Destroy all elements on the block starting from `first` itself.
    // This function will update everything about the hole info.
    NEST_FORCEINLINE Skipfield remove( const_iterator first ) noexcept
    {
      const auto num_deleted = wipe( first );
      if ( first.block_->occupied > 0 )
        // `wipe` deletes all holes accessed during traversal, including the last one;
        // therefore, clearly, no valid hole exists after first once `wipe` has been executed
        add_prefix_hole( first.block_, first.offset(), first.block_->capacity - first.offset() );
      else
        first.block_->first_hole = Index::npos;
      return num_deleted;
    }
    // Destroy all valid elements within [first, last), the iterator must point to a same block.
    // This function will update everything about the hole info.
    NEST_FORCEINLINE Skipfield remove( const_iterator first, const_iterator last ) noexcept
    {
      const auto num_deleted = wipe( first, last );
      if ( first.block_->occupied > 0 )
        add_prefix_hole( first.block_, first.offset(), last.offset() - first.offset() );
      else
        first.block_->first_hole = Index::npos;
      return num_deleted;
    }
    // Destroy all the content in the block.
    // This function will delete the entire hole linked list.
    NEST_FORCEINLINE Skipfield remove( Unit block ) noexcept
    {
      assert( block->occupied > 0 );
      purge( iterator::start_of( block ) );
      block->first_hole = Index::npos;
      return std::exchange( block->occupied, 0 );
    }

    // Eliminate all the elements and blocks in the list.
    size_type eliminate( BlockAlloc& block_alloc, AreaAlloc& area_alloc, Unit& busy ) noexcept
    {
      assert( busy != nullptr );
      size_type num_eliminated = 0;
      do {
        purge( iterator::start_of( busy ) );
        num_eliminated += busy->capacity;
        deallocate( block_alloc, area_alloc, std::exchange( busy, busy->next ) );
      } while ( busy != nullptr );
      return num_eliminated;
    }

    // Relocate the element from `src` to `dst`, the number of relocated elements will not exceed `limits`.
    // This function will not advance the `src` to the next block.
    template<typename U, bool Const>
    U* relocate( U* dst, Iterator<Const>& src, Skipfield limits )
    { // Currently, U is either Payload or Tp.
      using Category = std::conditional_t<Const, const Tp&, Tp&&>;
      assert( src != Iterator<Const>() );
      assert( src.exhausted() == false );
      assert( dst != nullptr );
      assert( limits > 0 );
      details::utils::attempt(
        [&] {
          if constexpr ( sizeof( U ) == sizeof( Payload ) // Payload is an union
                         && details::traits::AllocatorTriviallyConstructible<Alloc, Tp, Category>
                         && std::is_trivially_copyable_v<Tp> ) {
            // If there are optimization opportunities, then adopt block-based iteration.
            do {
              if ( const auto visible_len = (std::min<size_type>)( limits, src.rest() );
                   ( src.element_ == src.block_->element
                     && ( src.block_->occupied == src.block_->capacity
                          || src.block_->first_hole == src.block_->occupied ) )
                   || details::utils::all_zero( std::span(
                     details::utils::launder_as<const Skipfield>( src.skipfield_ ),
                     details::utils::pointer_cast<const Skipfield*>( src.skipfield_ ) + visible_len ) ) ) {
                // A trivially-copyable range can be bulk-copied only when the visible portion
                // contains no erased slots.
                // At the beginning of a block we can establish this from the block's occupancy metadata;
                // otherwise the skipfield must be zero throughout the visible range.
                const auto num_copied = (std::min<size_type>)( src.block_->occupied, visible_len );
                std::memcpy( static_cast<void*>( dst ),
                             details::utils::pointer_cast<void*>( src.element_ ),
                             num_copied * sizeof( Payload ) );
                dst += num_copied;
                limits -= visible_len;
                if ( !src.advance( visible_len ) )
                  // we get the sentinel of the current block.
                  break;
              } else
                do {
                  if constexpr ( Const )
                    std::allocator_traits<Alloc>::construct( this->allocator(), dst, *src );
                  else if constexpr ( !std::is_copy_constructible_v<Tp> )
                    std::allocator_traits<Alloc>::construct( this->allocator(), dst, std::move( *src ) );
                  else
                    std::allocator_traits<Alloc>::construct( this->allocator(),
                                                             dst,
                                                             std::move_if_noexcept( *src ) );
                  ++dst;
                  --limits;
                  if ( !src.advance() )
                    break;
                } while ( limits > 0 );
            } while ( limits > 0 && !src.exhausted() );
          } else
            // For types that are not trivially copyable,
            // here is no difference between manually looping and invoking `std::uninitialized_*`
            // functions, as both approaches ultimately call placement new (if the allocator does not
            // provide a construct function).
            do {
              if constexpr ( Const )
                std::allocator_traits<Alloc>::construct( this->allocator(), dst, *src );
              else if constexpr ( !std::is_copy_constructible_v<Tp> )
                std::allocator_traits<Alloc>::construct( this->allocator(), dst, std::move( *src ) );
              else
                std::allocator_traits<Alloc>::construct( this->allocator(),
                                                         dst,
                                                         std::move_if_noexcept( *src ) );
              ++dst;
              if ( !src.advance() )
                break;
            } while ( --limits > 0 );
        },
        [&, origin = dst] {
          if ( origin != dst ) {
            if constexpr ( std::is_same_v<U, Payload> )
              purge( Accessor( std::launder( origin ) ), dst );
            else
              purge( std::launder( origin ), dst );
          }
        } );
      return dst;
    }
    template<typename U, bool Const>
    U* relocate( U* dest, Iterator<Const>&& src, Skipfield limits )
    { return relocate( dest, src, limits ); }

    // Transfer the elements on the block pointed to by `src` to the block pointed to by dest.
    // When an exception is thrown, this method will not attempt to destruct the already completed elements,
    // but will ensure the integrity of the target block.
    template<std::derived_from<Filler> Output, bool Const>
    Skipfield transfer( Output& dst, Iterator<Const>& src )
    {
      assert( dst.block_ != src.block_ );
      assert( dst.block_->occupied < dst.block_->capacity );
      assert( src.block_->occupied > 0 );
      Skipfield total_relocated = 0;
      return details::utils::ensure(
        [&] {
          do {
            const auto target          = details::utils::pointer_cast<Payload*>( dst.element_ );
            const auto skipfield       = dst.skipfield_;
            const auto tail            = relocate( target, src, dst.rest() );
            const auto num_constructed = tail - target;
            std::memset( details::utils::pointer_cast<void*>( skipfield ),
                         0,
                         num_constructed * sizeof( Skipfield ) );
            total_relocated += num_constructed;
            if ( !dst.advance( num_constructed ) )
              break;
          } while ( !src.exhausted() );
          return total_relocated;
        },
        [&] {
          dst.block_->occupied += total_relocated;
          if ( total_relocated > 0 && dst.rest() > 0 ) {
            if constexpr ( std::is_same_v<Output, Filler> )
              build_hole( dst.block_, dst.offset(), dst.rest() );
            else
              fix_hole( dst.skipfield_, dst.rest() );
          }
        } );
    }
    template<bool Const>
    Skipfield transfer( std::derived_from<Filler> auto& dest, Iterator<Const>&& src )
    { return transfer( dest, src ); }

    /// @brief Due to the operation sequence, if there is a hollow block, it must be the first block of List.
    ///        And there is only one hollow block in the List.
    /// @return (List, total_size)
    template<bool Const>
    std::pair<Unit, size_type> migrate( Unit& idle, Iterator<Const>& source )
    {
      size_type num_transferred = 0;
      Unit list                 = nullptr;
      auto hole                 = Filler::from( idle );
      details::utils::attempt(
        [&] {
          while ( true ) {
            while ( true ) {
              const auto target          = details::utils::pointer_cast<Payload*>( hole.element_ );
              const auto next_dest       = relocate( target, source, hole.rest() );
              const auto num_constructed = static_cast<Skipfield>( next_dest - target );
              num_transferred += num_constructed;
              hole.block_->occupied += num_constructed;
              const auto exhausted = source.exhausted() && !source.next();
              if ( !hole.advance( num_constructed ) || exhausted )
                break;
              assert( details::utils::pointer_cast<Payload*>( hole.element_ ) == next_dest );
            }

            if ( hole.block_->occupied == hole.block_->capacity ) {
              std::memset( details::utils::pointer_cast<void*>( hole.block_->skipfield ),
                           0,
                           hole.block_->capacity * sizeof( Skipfield ) );
              attach( list, detach( idle ) );
              if ( idle == nullptr )
                break;
              hole = Filler::from( idle );
            } else if ( source.exhausted() ) {
              assert( hole.block_->occupied < hole.block_->capacity );
              std::memset( details::utils::pointer_cast<void*>( hole.block_->skipfield ),
                           0,
                           hole.skipfield_ - hole.block_->skipfield );
              build_hole( hole.block_, hole.offset(), hole.block_->capacity - hole.offset() );
              attach( list, detach( idle ) );
              break;
            }
          }
        },
        [&, origin = source] {
          // Revert the modifications to form a complete transaction.
          if ( hole.block_->occupied > 0 )
            purge( Accessor::from_object( hole.block_->element ), Accessor::from_address( hole.element_ ) );
          source = origin;
          while ( list != nullptr ) {
            purge( iterator::start_of( list ) );
            attach( idle, detach( list ) );
          }
        } );

      return { list, num_transferred };
    }

    /// @brief Reconstruct all the blocks from the source.
    /// @return See `transfer_to`.
    template<bool Const>
    std::pair<Unit, size_type> rebuild_from( BlockAlloc& block_alloc,
                                             AreaAlloc& area_alloc,
                                             Iterator<Const> source,
                                             size_type total_size,
                                             HiveLimits limits )
    {
      auto [blanks, total_capacity] = reserve_for( block_alloc, area_alloc, total_size, limits );
      return details::utils::attempt(
        [&] {
          const auto [list, num_transferred] = migrate( blanks, source );
          assert( num_transferred == total_size );
          return std::make_pair( list, total_capacity );
        },
        [&] {
          assert( blanks != nullptr );
          discard( block_alloc, area_alloc, blanks );
        } );
    }

    template<std::input_iterator Iter>
    void fill_with( Payload* dest, Iter& src, Skipfield count )
    {
      if constexpr ( _trivially_constructible_v<std::iter_reference_t<Iter>> ) {
        std::uninitialized_copy_n( src, count, dest );
        std::advance( src, count );
      } else {
        Skipfield num_constructed = 0;
        details::utils::attempt(
          [&] {
            do {
              std::allocator_traits<Alloc>::construct( this->allocator(), dest, *src );
              ++src;
              ++dest;
              ++num_constructed;
            } while ( num_constructed < count );
          },
          [&, origin = dest] {
            if ( num_constructed > 0 )
              purge( Accessor::from_object( origin ), Accessor( dest ) );
          } );
      }
    }
    void fill_with( Payload* dest, const Tp& value, Skipfield count )
    {
      if constexpr ( _trivially_constructible_v<const Tp&> )
        std::uninitialized_fill_n( dest, count, value );
      else {
        Skipfield num_constructed = 0;
        details::utils::attempt(
          [&] {
            do {
              std::allocator_traits<Alloc>::construct( this->allocator(), dest, value );
              ++dest;
              ++num_constructed;
            } while ( num_constructed < count );
          },
          [&, origin = dest] {
            if ( num_constructed > 0 )
              purge( Accessor::from_object( origin ), Accessor( dest ) );
          } );
      }
    }

    // Convert an empty block into a hollow block.
    template<typename... Args>
      requires std::is_constructible_v<Tp, Args...>
    NEST_FORCEINLINE iterator fill_blank( Unit blank, Args&&... args )
    {
      assert( blank->occupied == 0 );
      assert( blank->capacity >= 1 );
      auto hole = Filler::from( blank );
      hole.occupy( this->allocator(), std::forward<Args>( args )... );
      ++hole.block_->occupied;
      build_hole( blank, 1, blank->capacity - 1 );
      return static_cast<iterator>( hole );
    }

    // Fill a batch data into an empty block and convert it into a hollow block (if count < capacity).
    void fill_blank( Unit blank, std::input_iterator auto& iter, Skipfield count )
    {
      assert( count > 0 );
      assert( blank->occupied == 0 );
      assert( count <= blank->capacity );
      fill_with( details::utils::pointer_cast<Payload*>( blank->element ), iter, count );
      std::memset( details::utils::pointer_cast<void*>( blank->skipfield ), 0, count * sizeof( Skipfield ) );
      blank->occupied = count;
      if ( count < blank->capacity )
        build_hole( blank, count, blank->capacity - count );
    }
    // Fill the empty blank with `const Tp&` and convert it into a hollow block (if count < capacity).
    void fill_blank( Unit blank, const Tp& value, Skipfield count )
    {
      assert( count > 0 );
      assert( blank->occupied == 0 );
      assert( count <= blank->capacity );
      fill_with( details::utils::pointer_cast<Payload*>( blank->element ), value, count );
      std::memset( details::utils::pointer_cast<void*>( blank->skipfield ), 0, count * sizeof( Skipfield ) );
      blank->occupied = static_cast<Skipfield>( count );
      if ( count < blank->capacity )
        build_hole( blank, count, blank->capacity - count );
    }

    // Rebuild the current hive from `cursor`, `*this` must be empty.
    // This function will first consider reusing the existing idle blocks.
    template<bool Const>
    void fill_from( Iterator<Const> cursor, size_type total_size )
    {
      assert( empty() == true );
      if ( vacuum_ != nullptr ) {
        auto [list, num_transferred] = migrate( vacuum_, cursor );
        if ( list->occupied < list->capacity )
          add_hollow( hollow_, list );
        concat( head_, list );
        total_size -= num_transferred;
      }
      details::utils::attempt(
        [&] {
          if ( !cursor.exhausted() ) {
            BlockAlloc block_alloc { this->allocator() };
            AreaAlloc area_alloc { this->allocator() };
            auto [list, new_capacity] = rebuild_from( block_alloc,
                                                      area_alloc,
                                                      cursor,
                                                      total_size,
                                                      HiveLimits( min_capacity_, max_capacity_ ) );
            if ( list->occupied < list->capacity )
              add_hollow( hollow_, list );
            concat( head_, list );
            capacity_ += new_capacity;
          }
        },
        [this] { clear(); } );
    }

    // Introduce elements into current hive, the Source is either a forward iterator or const Tp&.
    template<typename Source>
      requires std::same_as<std::decay_t<Source>, Tp> || std::input_iterator<std::decay_t<Source>>
    void introduce( Source&& source, size_type count )
    {
      if ( hollow_ != nullptr ) {
        auto hole = Muncher::from( hollow_ );
        while ( true ) {
          const auto num_constructed = (std::min<size_type>)( hole.rest(), count );
          fill_with( details::utils::pointer_cast<Payload*>( hole.element_ ), source, num_constructed );
          std::memset( details::utils::pointer_cast<void*>( hole.block_->skipfield ),
                       0,
                       num_constructed * sizeof( Skipfield ) );
          hole.block_->occupied += num_constructed;
          count -= num_constructed;
          if ( !hole.advance( num_constructed ) ) {
            assert( hole.block_->occupied == hole.block_->capacity );
            remove_hollow( hollow_, hole.block_ );
            if ( count == 0 || hollow_ == nullptr )
              break;
            hole = Muncher::from( hollow_ );
          } else if ( count == 0 ) {
            assert( hole.rest() > 0 );
            fix_hole( hole.skipfield_, hole.rest() );
            return;
          }
        }
      }

      if ( count == 0 )
        return;
      const auto consume = [&]( Unit& list ) {
        const auto num_constructed = (std::min<size_type>)( list->capacity, count );
        fill_blank( list, source, static_cast<Skipfield>( num_constructed ) );
        occupied_ += num_constructed;
        count -= num_constructed;
        if ( num_constructed < list->capacity )
          add_hollow( hollow_, list );
        attach( head_, detach( list ) );
        if ( count == 0 )
          return false;
        return true;
      };

      while ( vacuum_ != nullptr )
        if ( !consume( vacuum_ ) )
          return;

      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      const auto [blanks, new_capacity] =
        reserve_for( block_alloc, area_alloc, count, HiveLimits( min_capacity_, max_capacity_ ) );
      concat( vacuum_, blanks );
      capacity_ += new_capacity;
      while ( true )
        if ( !consume( vacuum_ ) )
          return;
    }
    template<std::input_iterator First>
    void introduce( First first, std::sentinel_for<First> auto sentinel )
    {
      // bulk insertion to avoid repeatedly rebuilding the index
      if ( hollow_ != nullptr ) {
        Skipfield num_written = 0;
        auto hole             = Muncher::from( hollow_ );
        details::utils::ensure(
          [&] {
            while ( true ) {
              hole.occupy( this->allocator(), *first );
              ++num_written;
              // The hole pointer must always point to the next element that has not yet been accessed
              const auto within_block = hole.advance();
              ++first;

              if ( !within_block ) {
                remove_hollow( hollow_, hole.block_ );
                if ( hollow_ != nullptr && first != sentinel ) {
                  hole.block_->occupied += num_written;
                  occupied_ += num_written;
                  num_written = 0;
                } else
                  break;
                hole = Muncher::from( hollow_ );
              } else if ( first == sentinel )
                break;
            }
          },
          [&]() noexcept {
            if ( num_written == 0 ) [[unlikely]]
              return;
            hole.block_->occupied += num_written;
            occupied_ += num_written;
            if ( hole.rest() > 0 )
              fix_hole( hole.skipfield_, hole.rest() );
          } );
      }

      if ( first == sentinel )
        return;
      const auto consume = [&]( Unit& list ) {
        auto hole = Filler::from( list );
        details::utils::ensure(
          [&] {
            do {
              hole.occupy( this->allocator(), *first );
              ++hole.block_->occupied;
              hole.advance();
              ++first;
            } while ( hole.rest() > 0 && first != sentinel );
          },
          [&]() noexcept {
            if ( hole.block_->occupied == 0 ) [[unlikely]]
              return;
            if ( hole.rest() > 0 )
              build_hole( hole.block_, hole.offset(), hole.rest() );
          } );
        if ( hole.rest() > 0 )
          add_hollow( hollow_, list );
        attach( head_, detach( list ) );
        occupied_ += hole.block_->occupied;
        return first != sentinel;
      };

      while ( vacuum_ != nullptr )
        if ( !consume( vacuum_ ) )
          return;

      BlockAlloc block_alloc { this->allocator() };
      AreaAlloc area_alloc { this->allocator() };
      do {
        attach( vacuum_, allocate( block_alloc, area_alloc, next_capacity_ ) );
        capacity_ += next_capacity_;
        next_capacity_ = grow( next_capacity_ );
      } while ( consume( vacuum_ ) );
    }

    template<typename Comp>
    void direct_sort( Tp* buffer, Comp&& comp )
    {
      auto tail = buffer; // used to mark the destruction zone
      details::utils::attempt(
        [&] {
          auto iter = begin();
          do
            tail = relocate( tail, iter, occupied_ );
          while ( tail < buffer + occupied_ );
          assert( tail == std::to_address( buffer ) + occupied_ );
          std::ranges::sort( buffer, buffer + occupied_, std::forward<Comp>( comp ) );

          iter = begin();
          do
            *( iter++ ) = std::move( *( buffer++ ) );
          while ( !iter.exhausted() );
        },
        [&, origin = buffer]() mutable {
          if constexpr ( !std::is_trivially_copyable_v<Tp> && std::is_nothrow_move_constructible_v<Tp>
                         && !std::is_nothrow_invocable_r_v<bool, Comp, Tp&, Tp&> ) {
            // According to the implementation of `relocate`,
            // at this point all elements have definitely been moved to the buffer,
            // and an exception was thrown during the sorting process;
            // we need to write all elements back.
            assert( tail == origin + occupied_ );
            // And the exception must be thrown by Comp, not by the construction process.
            for ( auto iter = begin(); !iter.exhausted(); ++iter )
              *iter = std::move( *( origin++ ) );
          } else
            (void)origin;
        },
        [&, origin = buffer]() noexcept {
          if ( origin != tail )
            purge( origin, tail );
        } );
    }
    template<typename Comp>
    void indirect_sort( Tp** buffer, size_type* mapping, Comp&& comp )
    {
      // used to mark the destruction zone of size_type (in case the size_type is not a scalar type)
      auto tail = mapping;
      details::utils::ensure(
        [&] {
          size_type count = 0;
          for ( auto iter = begin(); iter != end(); ++iter, ++tail, ++count ) {
            // the pointer is a scalar type, we don't need to construct it.
            buffer[count] = std::addressof( *iter );
            std::construct_at( tail, count );
          }

          std::ranges::sort( mapping, tail, std::forward<Comp>( comp ), [&]( size_type i ) noexcept -> Tp& {
            return *buffer[i];
          } );

          // permutation cycle
          count = 0;
          do {
            while ( mapping[count] != count ) {
              std::iter_swap( buffer[count], buffer[mapping[count]] );
              using std::swap;
              swap( mapping[count], mapping[mapping[count]] );
            }
            ++count;
          } while ( count < occupied_ );
        },
        [&] {
          if ( !std::is_trivially_destructible_v<size_type> )
            while ( mapping != tail )
              std::destroy_at( mapping++ );
        } );
    }

    constexpr void interchange( Hive& other )
    {
      using std::swap;
      swap( head_, other.head_ );
      swap( vacuum_, other.vacuum_ );
      swap( hollow_, other.hollow_ );
      swap( occupied_, other.occupied_ );
      swap( capacity_, other.capacity_ );
      swap( max_capacity_, other.max_capacity_ );
      swap( min_capacity_, other.min_capacity_ );
      swap( next_capacity_, other.next_capacity_ );
    }

    size_type _erase_if( auto&& pred )
    {
      if ( empty() )
        return 0;

      size_type total_erased     = 0;
      size_type currently_erased = 0;
      iterator first;
      auto cursor = begin();
      while ( true ) {
        // block-based iteration
        const bool satisfied    = std::invoke( pred, *cursor );
        // The most recently identified element, not the last-tail element.
        const auto last         = cursor;
        const bool within_block = cursor.advance();
        currently_erased += static_cast<size_type>( satisfied );
        if ( satisfied && currently_erased == 1 )
          first = last;
        if ( !satisfied || !within_block ) {
          if ( currently_erased == 0 ) {
            assert( satisfied == false );
            if ( within_block || cursor.next() )
              continue;
            break;
          }

          const auto no_more_block = !within_block && !cursor.next();
          const auto full          = first.block_->occupied == first.block_->capacity;
          if ( satisfied && !within_block )
            remove( first );
          else if ( satisfied )
            remove( first, std::next( last ) );
          else
            remove( first, last );
          if ( first.block_->occupied == 0 ) {
            if ( !full )
              remove_hollow( hollow_, first.block_ );
            if ( first.block_ != head_ ) {
              unlink( head_, first.block_ );
              attach( vacuum_, first.block_ );
            } else
              attach( vacuum_, detach( head_ ) );
          } else if ( full ) {
            assert( first.block_->occupied < first.block_->capacity );
            add_hollow( hollow_, first.block_ );
          }
          occupied_ -= currently_erased;
          total_erased += currently_erased;
          if ( !within_block && no_more_block )
            break;
          currently_erased = 0;
        }
      }
      return total_erased;
    }
    size_type _erase( const Tp& value )
    {
      return _erase_if( [&]( const auto& ele ) { return ele == value; } );
    }

    // Since `erase_if` relies on certain friend entities that are only valid within Hive itself
    // (i.e., internal functions of Iterator), this function cannot be implemented outside the class,
    // nor can the class's friend be templated (which would lead to ODR violations).
    // Therefore, it must be implemented as an internal hidden method with external invocation.
    template<typename U, typename A, typename Pred>
    friend Hive<U, A>::size_type erase_if( Hive<U, A>&, Pred );
    template<typename U, typename A>
    friend Hive<U, A>::size_type erase( Hive<U, A>&, const U& );
  };

  template<typename InputIt, typename Alloc = std::allocator<std::iter_value_t<InputIt>>>
  Hive( InputIt, InputIt, Alloc = Alloc() ) -> Hive<std::iter_value_t<InputIt>, Alloc>;

  template<typename InputIt, typename Alloc = std::allocator<std::iter_value_t<InputIt>>>
  Hive( InputIt, InputIt, HiveLimits, Alloc = Alloc() ) -> Hive<std::iter_value_t<InputIt>, Alloc>;

#ifdef __cpp_lib_containers_ranges
  template<std::ranges::input_range R, typename Alloc = std::allocator<std::ranges::range_value_t<R>>>
  Hive( std::from_range_t, R&&, Alloc = Alloc() ) -> Hive<std::ranges::range_value_t<R>, Alloc>;

  template<std::ranges::input_range R, typename Alloc = std::allocator<std::ranges::range_value_t<R>>>
  Hive( std::from_range_t, R&&, HiveLimits, Alloc = Alloc() ) -> Hive<std::ranges::range_value_t<R>, Alloc>;
#endif

  template<typename T, typename Alloc, typename Pred>
  NEST_FORCEINLINE Hive<T, Alloc>::size_type erase_if( Hive<T, Alloc>& hive, Pred pred )
  { return hive._erase_if( std::move( pred ) ); }
  template<typename T, typename Alloc>
  NEST_FORCEINLINE Hive<T, Alloc>::size_type erase( Hive<T, Alloc>& hive, const T& value )
  { return hive._erase( value ); }
} // namespace nest

#endif
