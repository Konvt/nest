#include "nest.hpp"
#include <chrono>
#include <complex>
#include <format>
#include <iomanip>
#include <iostream>
#include <list>
#include <numeric>
#include <print>

#if __cplusplus < 202302L
# error "require C++23"
#endif

template<typename Tp, typename Alloc>
struct std::formatter<nest::Hive<Tp, Alloc>, char> {
  template<class ParseContext>
  constexpr auto parse( ParseContext& ctx )
  {
    auto it = ctx.begin();
    while ( it != ctx.end() && *it != '}' )
      ++it;
    return it;
  }

  template<class FmtContext>
  auto format( const nest::Hive<Tp, Alloc>& hive, FmtContext& ctx ) const
  {
    auto out = ctx.out();
    *out++   = '[';

    bool first = true;
    for ( const auto& ele : hive ) {
      if ( !first ) {
        *out++ = ',';
        *out++ = ' ';
      }
      first = false;
      out   = std::format_to( out, "{}", ele );
    }

    *out++ = ']';
    return out;
  }
};

template<typename T>
struct std::formatter<std::complex<T>, char> {
  template<class ParseContext>
  constexpr ParseContext::iterator parse( ParseContext& ctx )
  { return ctx.begin(); }

  template<class FmtContext>
  FmtContext::iterator format( const auto& s, FmtContext& ctx ) const
  {
    auto out { std::format( "({},{})", s.real(), s.imag() ) };
    return std::ranges::copy( std::move( out ), ctx.out() ).out;
  }
};

template<typename T>
void assert_equal( const auto& x, std::initializer_list<T>&& y )
{ assert( std::ranges::equal( x, y ) ); }

struct Bee {
  std::string s;

  Bee( std::string str ) : s( std::move( str ) ) { std::println( R"(  Constructed, s: "{}")", s ); }
  Bee( const Bee& o ) : s( o.s ) { std::println( R"(  Copy constructed, s: "{}")", s ); }
  Bee( Bee&& o ) : s( std::move( o.s ) ) { std::println( R"(  Move constructed, s: "{}")", s ); } // NOLINT

  Bee& operator=( const Bee& other )
  {
    s = other.s;
    std::println( R"(  Copy assigned, s: "{}")", s );
    return *this;
  }
  Bee& operator=( Bee&& other ) // NOLINT
  {
    s = std::move( other.s );
    std::println( R"(  Move assigned, s: "{}")", s );
    return *this;
  }
};

struct Honeybee {
  int id {};
};

int main()
{ // The codes below is copied from cppreference.com.
  {
    std::println( "---------- constructor test ----------" );
    const std::size_t count { 4 };
    nest::Hive<int> h1( count ); // (5) hive(size_type count)
    assert( h1.size() == count );

    const std::size_t value { 42 };
    nest::Hive<int> h2( count, value ); // (7) hive(size_type count, const T& value)
    assert_equal( h2, { 42, 42, 42, 42 } );

    nest::Hive<int> h3( h2.begin(), h2.end() ); // (9) hive(InputIter first, InputIter last)
    assert_equal( h3, { 42, 42, 42, 42 } );

    const auto list = { 10, 11, 12 };
    nest::Hive<int> h4( std::from_range, list ); // (11) hive(from_range_t, R&&)
    assert_equal( h4, { 10, 11, 12 } );

    nest::Hive<std::string> s1 { "1", "2", "3" };
    auto h5 = nest::Hive( s1 ); // (13) hive(const hive& other)
    assert_equal( h5, { "1", "2", "3" } );

    auto h6 = nest::Hive( std::move( h5 ) ); // (14) hive(hive&& other)
    assert_equal( h6, { "1", "2", "3" } );
    std::println( "h5 = {} (after move out)", h5 );

    nest::Hive<int> h7( list ); // (18) hive(initializer_list<T> ilist)
    assert_equal( h7, { 10, 11, 12 } );
  }
  {
    std::println( "---------- operator= test ----------" );
    nest::Hive<int> a { 1, 2, 3 }, b, c;
    std::println( "Initially:" );
    std::println( "a = {}", a );
    std::println( "b = {}", b );
    std::println( "c = {}", c );

    b = a; // overload (1)
    std::println( "Copy assignment copies data from a to b:" );
    std::println( "a = {}", a );
    std::println( "b = {}", b );

    c = std::move( a ); // overload (2)
    std::println( "Move assignment moves data from a to c, modifying both a and c:" );
    std::println( "a = {}", a );
    std::println( "c = {}", c );

    c = { 4, 5, 6, 7 }; // overload (3)
    std::println( "Assignment of initializer_list to c:" );
    std::println( "c = {}", c );
  }
  {
    std::println( "---------- assign test ----------" );
    nest::Hive<int> hive;

    const std::size_t count { 4 };
    const int value { 5 };
    hive.assign( count, value );
    std::println( "{}", hive );

    const auto list = { 1, 2, 3, 4 };
    hive.assign( list.begin(), list.end() );
    std::println( "{}", hive );

    hive.assign( { 5, 6, 7, 8 } );
    std::println( "{}", hive );
  }
  {
    std::println( "---------- assign_range test ----------" );
    const auto source = std::list { 2, 7, 1 };
    auto destination  = nest::Hive { 3, 1, 4 };
    destination.assign_range( source );
    assert( std::ranges::equal( source, destination ) );

    assert_equal( std::views::iota( 1, 6 ) | std::views::transform( []( int n ) { return n * 2; } )
                    | std::ranges::to<nest::Hive>(),
                  { 2, 4, 6, 8, 10 } );
  }
  {
    std::println( "---------- iterators test ----------" );
    nest::Hive h { "bee", "wasp", "hornet" };

    while ( h.cbegin() != h.cend() ) {
      std::println( "{}", *h.cbegin() );
      h.erase( h.begin() );
    }

    const nest::Hive<int> hive { 1, 2, 3, 4, 5 };
    std::copy( hive.rbegin(), hive.rend(), std::ostream_iterator<int>( std::cout, " " ) );
    std::cout << '\n';

    nest::Hive<int> nums { 1, 2, 4, 8, 16 };
    nest::Hive<std::string> fruits { "orange", "apple", "raspberry" };
    nest::Hive<char> empty;

    // Print hive nums.
    std::ranges::for_each( nums | std::views::reverse, []( const int n ) { std::cout << n << ' '; } );
    std::cout << '\n';

    // Sum all integers in the hive nums, printing only the result.
    std::cout << "Sum of nums: " << std::accumulate( nums.crbegin(), nums.crend(), 0 ) << '\n';

    // Print the last fruit in the hive fruits, checking if there is any.
    if ( !fruits.empty() )
      std::cout << "Last fruit: " << *fruits.crbegin() << '\n';

    if ( empty.crbegin() == empty.crend() )
      std::cout << "hive 'empty' is indeed empty.\n";

    // Modify the last element in nums.
    *nums.rbegin() = 32;
    assert( *nums.crbegin() == 32 );
  }
  {
    std::println( "---------- empty test ----------" );
    std::cout << std::boolalpha;

    nest::Hive<int> bees;
    std::cout << "Initially, bees.empty() == " << bees.empty() << '\n';

    bees.insert( 00 );
    std::cout << "After adding elements, bees.empty() == " << bees.empty() << '\n';
  }
  {
    std::println( "---------- size test ----------" );
    nest::Hive<int> bees;
    assert( bees.size() == 0 );
    bees = { 1, 2, 3, 4 };
    assert( bees.size() == 4 );
  }
  {
    std::println( "---------- max_size test ----------" );
    nest::Hive<char> p;
    nest::Hive<long> q;

    std::cout.imbue( std::locale( "en_US.UTF-8" ) );
    std::cout << std::uppercase << "p.max_size() = " << std::dec << p.max_size() << " = 0x" << std::hex
              << p.max_size() << '\n'
              << "q.max_size() = " << std::dec << q.max_size() << " = 0x" << std::hex << q.max_size() << '\n'
              << std::dec;
  }
  {
    std::println( "---------- capacity test ----------" );
    int sz = 1000;
    nest::Hive<int> v;

    auto cap = v.capacity();
    std::cout << "Initial size: " << v.size() << ", capacity: " << cap << '\n';

    std::cout << "\nDemonstrate the capacity's growth policy."
                 "\nSize:  Capacity:  Ratio:\n"
              << std::left;
    while ( sz-- > 0 ) {
      v.insert( sz );
      if ( cap != v.capacity() ) {
        std::cout << std::setw( 7 ) << v.size() << std::setw( 11 ) << v.capacity() << std::setw( 10 )
                  << v.capacity() / static_cast<float>( cap ) << '\n';
        cap = v.capacity();
      }
    }

    std::cout << "\nFinal size: " << v.size() << ", capacity: " << v.capacity() << '\n';
  }
  {
    std::println( "---------- reserve test ----------" );
    constexpr int max_elements = 1'000;

    std::println( "Using reserve:" );
    {
      nest::Hive<int> h;
      h.reserve( max_elements );

      for ( int n {}; n != max_elements; ++n )
        h.insert( n );

      std::println( "size(): {}, capacity(): {}", h.size(), h.capacity() );
    }

    std::println( "Not using reserve:" );
    {
      nest::Hive<int> h;

      for ( int n {}; n != max_elements; ++n ) {
        if ( h.size() == h.capacity() )
          std::println( "size() == capacity() == {}", h.size() );
        h.insert( n );
      }

      std::println( "size(): {}, capacity(): {}", h.size(), h.capacity() );
    }
  }
  {
    std::println( "---------- shrink_to_fit test ----------" );
    {
      nest::Hive<int> hive;
      std::println( "Default-constructed capacity is {}", hive.capacity() );
      hive.insert( 100, 0 );
      std::println( "Capacity after inserting 100 elements is {}", hive.capacity() );
      for ( auto i { 0 }; i != 42; ++i )
        hive.erase( hive.begin() );
      std::println( "Capacity after erasing 42 elements is {}, size is {}", hive.capacity(), hive.size() );
      hive.shrink_to_fit();
      std::println( "Capacity after shrink_to_fit() is {}", hive.capacity() );
    }
    {
      constexpr int odd  = 3;
      constexpr int even = 2;

      // Construct a sequence sufficient to generate multiple blocks.
      auto hive = std::views::iota( 0 )
                | std::views::transform( [=]( int x ) { return ( x % 2 == 0 ) ? odd : even; } )
                | std::views::take( 500 ) | std::ranges::to<nest::Hive>();
      std::println( "Capacity after inserting 500 elements is {}", hive.capacity() );
      using std::erase;
      erase( hive, odd ); // test ADL
      std::println( "Capacity after erasing 250 elements is {}, size is {}", hive.capacity(), hive.size() );
      hive.shrink_to_fit();
      std::println( "Capacity after shrink_to_fit() is {}", hive.capacity() );
      assert( std::ranges::equal( hive, std::views::repeat( even ) | std::views::take( 250 ) ) );
    }
  }
  {
    std::println( "---------- trim_capacity test ----------" );
    nest::Hive<int> hive;

    const std::size_t count { 1000 };
    const int value { 100 };

    hive.insert( count, value );
    hive.insert( count, value + 1 );
    std::println( "After inserting {} elements capacity is {}", hive.size(), hive.capacity() );

    nest::erase( hive, value );
    std::println( "After erasing {} elements capacity is {}", count, hive.capacity() );

    auto hive2           = hive;
    const auto cap_limit = hive2.capacity() * 2;
    hive2.reserve( hive2.capacity() * 5 );
    std::println( "The capacity of hive2 is {}", hive2.capacity() );

    hive.shrink_to_fit();
    std::println( "After shrink_to_fit() capacity is {}", hive.capacity() );

    hive2.trim_capacity( cap_limit );
    std::println( "After trim_capacity({}) capacity is {}", cap_limit, hive2.capacity() );
    hive2.trim_capacity();
    std::println( "After trim_capacity() capacity is {}", hive2.capacity() );
  }
  {
    std::println( "---------- block_capacity test ----------" );
    nest::Hive<int> colony( 100'500 );
    const auto limits1 { colony.block_capacity_limits() };
    std::cout << limits1.min << ' ' << limits1.max << '\n';

    constexpr auto limits2 { nest::Hive<int>::block_capacity_default_limits() };
    std::cout << limits2.min << ' ' << limits2.max << '\n';

    constexpr auto limits3 { nest::Hive<int>::block_capacity_hard_limits() };
    std::cout << limits3.min << ' ' << limits3.max << '\n';

    using Garage = nest::Hive<int>;

    static_assert( Garage::is_within_hard_limits( Garage::block_capacity_hard_limits() )
                   && Garage::is_within_hard_limits( Garage::block_capacity_default_limits() ) );

    auto limits4 { nest::Hive<int>::block_capacity_hard_limits() };

    auto print = []( nest::HiveLimits limits4 ) {
      std::cout << "Limits {min: " << limits4.min << ", max: " << limits4.max << "} is ";
      std::cout << ( Garage::is_within_hard_limits( limits4 ) ? "within" : "without" )
                << " block capacity hard limits\n";
    };
    print( limits4 );
    limits4.min = 0;
    print( limits4 );
  }
  {
    std::println( "---------- reshape test ----------" );

    using Garage    = nest::Hive<int>;
    const auto hard = Garage::block_capacity_hard_limits();
    std::println( "Hard limits: min={}, max={}", hard.min, hard.max );

    // Helper to check that the hive's limits match expectations.
    auto check_limits = []( const Garage& h, nest::HiveLimits expected ) {
      auto actual = h.block_capacity_limits();
      assert( actual.min == expected.min && actual.max == expected.max );
    };

    // Helper to produce valid limits within hard limits.
    auto valid_limits = [&]( size_t min_factor, size_t max_factor ) {
      size_t min = std::clamp( min_factor, hard.min, hard.max );
      size_t max = std::clamp( std::max( min_factor, max_factor ), hard.min, hard.max );
      if ( min > max )
        std::swap( min, max );
      return nest::HiveLimits { min, max };
    };

    // Helper to verify that two hives contain the same multiset of elements.
    auto same_elements = []( const std::ranges::range auto& a, const std::ranges::range auto& b ) {
      if ( std::ranges::size( a ) != std::ranges::size( b ) )
        return false;
      auto va = a | std::ranges::to<std::vector>();
      auto vb = b | std::ranges::to<std::vector>();
      std::ranges::sort( va );
      std::ranges::sort( vb );
      return va == vb;
    };

    // Case 1: empty hive – only limits should change.
    {
      nest::Hive<int> empty;
      const auto default_limits = empty.block_capacity_limits();
      std::println( "Empty hive initial limits: min={}, max={}", default_limits.min, default_limits.max );

      const auto new_limits = valid_limits( 10, 100 );
      empty.reshape( new_limits );
      check_limits( empty, new_limits );
      assert( empty.empty() );
      std::println( "Empty hive reshaped to min={}, max={}", new_limits.min, new_limits.max );
    }
    // Case 2: single element – reshaped limits must be respected,
    //         element must remain (order irrelevant).
    {
      const auto original = std::views::single( 42 );
      nest::Hive<int> single { 42 };
      auto new_limits = valid_limits( 20, 200 );
      single.reshape( new_limits );
      check_limits( single, new_limits );
      assert( same_elements( single, original ) );
    }
    // Case 3: multiple blocks – force a reshape that will reorganise
    //         the internal blocks by choosing a small max capacity.
    //         Only the multiset of elements must be preserved.
    {
      constexpr size_t N  = 1000;
      const auto original = std::views::iota( 0, static_cast<int>( N ) );
      auto hive           = original | std::ranges::to<Garage>();

      auto tight_limits = valid_limits( 8, 32 );
      hive.reshape( tight_limits );
      check_limits( hive, tight_limits );

      assert( same_elements( hive, original ) );
      std::println( "Reshaped to tight limits (min={}, max={}), "
                    "size={}, capacity={}",
                    tight_limits.min,
                    tight_limits.max,
                    hive.size(),
                    hive.capacity() );
    }
    // Case 4: reshape to a larger minimum capacity – should also work.
    {
      const auto original = std::views::iota( 0, 50 );
      auto hive           = original | std::ranges::to<Garage>();
      auto large_limits   = valid_limits( 60, 200 );
      std::println( "Initial:\n{}", hive );

      hive.reshape( large_limits );
      std::println( "After reshaping:\n{}", hive );

      check_limits( hive, large_limits );
      assert( same_elements( hive, original ) );
      std::println( "Reshaped to large limits (min={}, max={}), "
                    "size={}, capacity={}",
                    large_limits.min,
                    large_limits.max,
                    hive.size(),
                    hive.capacity() );
    }
    // Case 5: invalid limits – must throw std::invalid_argument.
    {
      nest::Hive<int> hive( 10, 0 );

      // min > hard.max
      try {
        hive.reshape( { hard.max + 1, hard.max + 10 } );
        assert( false && "Should have thrown" );
      } catch ( const std::invalid_argument& e ) {
        std::println( "Caught expected exception: {}", e.what() );
      }

      // max < hard.min
      try {
        hive.reshape( { 1, hard.min - 1 } );
        assert( false && "Should have thrown" );
      } catch ( const std::invalid_argument& e ) {
        std::println( "Caught expected exception: {}", e.what() );
      }

      // The assertion in `HiveLimits` will reject illegal ranges (where `min` is greater than `max`),
      // this situation does not require testing.
    }
    // Case 6: reshape to completely different ranges (no overlap with default limits).
    {
      const auto original = std::views::iota( 0, 500 );
      auto hive           = original | std::ranges::to<Garage>();
      std::println( "Default limits: ({}, {})",
                    Garage::block_capacity_default_limits().min,
                    Garage::block_capacity_default_limits().max );

      // Range entirely below the default minimum (~52).
      auto lower_limits = valid_limits( 10, 30 );
      hive.reshape( lower_limits );
      check_limits( hive, lower_limits );
      assert( same_elements( hive, original ) );
      std::println( "Reshaped to lower limits (min=10, max=30), "
                    "size={}, capacity={}",
                    hive.size(),
                    hive.capacity() );

      // Range entirely above the default minimum.
      auto upper_limits = valid_limits( 150, 250 );
      hive.reshape( upper_limits );
      check_limits( hive, upper_limits );
      assert( same_elements( hive, original ) );
      std::println( "Reshaped to upper limits (min=150, max=250), "
                    "size={}, capacity={}",
                    hive.size(),
                    hive.capacity() );
    }
  }
  {
    std::println( "---------- emplace test ----------" );
    nest::Hive<Bee> hive;

    std::println( "Construct Bee twice:" );
    Bee two { "two" };
    Bee three { "three" };

    std::println( "Emplace:" );
    hive.emplace( "one" );

    std::println( "Emplace with Bee&:" );
    hive.emplace( two );

    std::println( "Emplace with Bee&&:" );
    hive.emplace( std::move( three ) );

    std::println( "Hive: {}", hive | std::views::transform( []( const Bee& o ) { return o.s; } ) );
  }
  {
    std::println( "---------- insert test ----------" );
    nest::Hive<int> colony( 3, 10 );
    std::println( "1. {}", colony );

    colony.insert( 20 ); // overload (1)
    std::println( "2. {}", colony );

    colony.insert( { 30, 31, 32 } ); // overload (5)
    std::println( "3. {}", colony );

    const auto list = { 40, 41, 42 };
    colony.insert( list.begin(), list.end() ); // overload (6)
    std::println( "4. {}", colony );

    colony.insert( 4, 50 ); // overload (7)
    std::println( "5. {}", colony );
  }
  {
    std::println( "---------- insert_range test ----------" );
    auto hive     = nest::Hive { 1, 2, 3, 4 };
    const auto rg = std::vector { -1, -2, -3 };

    hive.erase( hive.begin() );
    std::println( "{}", hive );

    hive.insert_range( rg );
    std::println( "{}", hive );
  }
  {
    std::println( "---------- erase test ----------" );
    nest::Hive v { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
    std::println( "{}", v );

    v.erase( v.begin() );
    std::println( "{}", v );

    v.erase( std::next( v.begin(), 2 ), std::next( v.begin(), 5 ) );
    std::println( "{}", v );

    // Erase all even numbers
    for ( auto it { v.begin() }; it != v.end(); )
      if ( *it % 2 == 0 )
        it = v.erase( it );
      else
        ++it;
    std::println( "{}", v );
  }
  {
    std::println( "---------- swap test ----------" );
    {
      nest::Hive<int> a1 { 1, 2, 3 }, a2 { 4, 5 };

      auto it1 = std::next( a1.begin() );
      auto it2 = std::next( a2.begin() );

      int& ref1 = *a1.begin();
      int& ref2 = *a2.begin();

      std::println( "{} {} {} {} {} {}", a1, a2, *it1, *it2, ref1, ref2 );
      a1.swap( a2 );
      std::println( "{} {} {} {} {} {}", a1, a2, *it1, *it2, ref1, ref2 );

      // Note that after swap the iterators and references stay associated with their
      // original elements, e.g., it1 that pointed to an element in 'a1' with value 2
      // still points to the same element, though this element was moved into 'a2'.
    }
    {
      nest::Hive<int> alice { 1, 2, 3 };
      nest::Hive<int> bob { 7, 8, 9, 10 };

      auto print = []( const int& n ) { std::cout << ' ' << n; };

      // Print state before swap
      std::cout << "Alice:";
      std::for_each( alice.begin(), alice.end(), print );
      std::cout << "\nBobby:";
      std::for_each( bob.begin(), bob.end(), print );
      std::cout << '\n';

      std::cout << "-- SWAP\n";
      using std::swap;
      swap( alice, bob );

      // Print state after swap
      std::cout << "Alice:";
      std::for_each( alice.begin(), alice.end(), print );
      std::cout << "\nBobby:";
      std::for_each( bob.begin(), bob.end(), print );
      std::cout << '\n';
    }
  }
  {
    std::println( "---------- clear test ----------" );
    nest::Hive<int> v { 1, 2, 3 };
    std::println( "Before clear: {}, size={}, capacity={}", v, v.size(), v.capacity() );
    v.clear();
    std::println( "After clear: {}, size={}, capacity={}", v, v.size(), v.capacity() );
  }
  {
    std::println( "---------- splice test ----------" );
    nest::Hive<int> hotel { 1, 2, 3 }, motel { 4, 5 };
    std::println( "Before splice: hotel = {}, motel = {}", hotel, motel );
    hotel.splice( motel );
    std::println( "After splice: hotel = {}, motel = {}", hotel, motel );
  }
  {
    std::println( "---------- unique test ----------" );
    nest::Hive<int> c { 1, 2, 2, 3, 3, 2, 1, 1, 2 };
    std::println( "Before unique(): {}", c );
    const auto count1 = c.unique();
    std::println( "After unique():  {} ({} elements removed)", c, count1 );
    assert_equal( c, { 1, 2, 3, 2, 1, 2 } );

    c = { 1, 2, 12, 23, 3, 2, 51, 1, 2, 2 };
    std::println( "Before unique(pred): {}", c );
    auto pred         = [mod = 10]( int x, int y ) { return ( x % mod ) == ( y % mod ); };
    const auto count2 = c.unique( pred );
    std::println( "After unique(pred):  {} ({} elements removed)", c, count2 );
    assert_equal( c, { 1, 2, 23, 2, 51, 2 } );
  }
  {
    std::println( "---------- sort test ----------" );
    nest::Hive<int> hive { 3, 1, 4, 1, 5, 9, 2, 6, 5 };
    std::println( "Initially:  {}", hive );

    hive.sort();
    std::println( "Ascending:  {}", hive );

    hive.sort( std::greater<int>() );
    std::println( "Descending: {}", hive );
  }
  {
    std::println( "---------- get_iterator test ----------" );
    nest::Hive<Honeybee> bees;

    // populate the hive
    for ( int id {}; id != 14; ++id )
      bees.emplace( id );

    // get the address of the last element
    Honeybee* bee = &*--bees.end();
    std::cout << "bee->id = " << bee->id << '\n';

    // remove all elements with even id
    nest::erase_if( bees, []( const Honeybee& bee ) { return ( bee.id % 2 ) == 0; } );

    auto iter = bees.get_iterator( bee );
    assert( iter == --bees.end() );
    std::cout << "iter->id = " << iter->id << '\n';
  }
  {
    std::println( "---------- erase & erase_if test ----------" );
    nest::Hive<char> cnt( 10 );
    std::iota( cnt.begin(), cnt.end(), '0' );
    std::println( "Initially, cnt = {}", cnt );

    nest::erase( cnt, '3' );
    std::println( "After erase '3', cnt = {}", cnt );
    assert( std::ranges::equal( cnt,
                                std::views::iota( 0, 10 )
                                  | std::views::transform( []( int x ) -> char { return '0' + x; } )
                                  | std::views::filter( []( char x ) { return x != '3'; } ) ) );

    auto erased = nest::erase_if( cnt, []( char x ) { return ( x - '0' ) % 2 == 0; } );
    std::println( "After erase all even numbers, cnt = {}", cnt );
    std::println( "Erased even numbers: {}", erased );
    assert( std::ranges::equal(
      cnt,
      std::views::iota( 0, 5 ) | std::views::transform( []( int x ) -> char { return '0' + ( x * 2 + 1 ); } )
        | std::views::filter( []( char x ) { return x != '3'; } ) ) );

    nest::Hive<std::complex<double>> nums {
      { 2, 2 },
      { 4, 2 },
      { 4, 8 },
      { 4, 2 }
    };
    nest::erase( nums, std::complex<double> { 4, 2 } );
    std::println( "After erase {{4, 2}}, nums = {}", nums );
  }
  {
    std::println( "---------- simple benchmark test ----------" );
    constexpr auto iterations = static_cast<unsigned>( 1e7 );
    auto benchmark            = [=]( auto& container, auto rem ) {
      const auto t0 { std::chrono::high_resolution_clock::now() };
      auto it { container.cbegin() };
      for ( auto n { 0 }; n != iterations; ++n )
        container.insert( it, n );
      while ( container.size() )
        container.erase( container.begin() );
      const std::chrono::duration<double> diff { std::chrono::high_resolution_clock::now() - t0 };
      std::println(
#ifdef NDEBUG
        "[Release] "
#else
        "[Debug] "
#endif
        "{:.3f}s {}",
        diff.count(),
        rem );
    };

    std::list<std::size_t> list;
    benchmark( list, " (cold list)" );
    benchmark( list, " (warm list)" );

    nest::Hive<std::size_t> hive;
    benchmark( hive, " (cold hive)" );
    benchmark( hive, " (warm hive)" );
  }
}
