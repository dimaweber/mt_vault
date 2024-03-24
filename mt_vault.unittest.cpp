#include <fmt/format.h>
#include <fmt/ostream.h>
#include <gtest/gtest.h>

#include <random>
#include <thread>

#include "my_vault.h"

struct Data {
    int         field_1 {0};
    std::string field_3;
};

std::ostream& operator<< (std::ostream& st, const Data& data)
{
    fmt::print(st, "s: {}  i: {}", data.field_3, data.field_1);
    return st;
}

void long_lasting_op ( )
{
    std::random_device                    dev;
    std::mt19937                          rng(dev( ));
    std::uniform_int_distribution<size_t> dist(1, 10);
    std::this_thread::sleep_for(std::chrono::nanoseconds {dist(rng)});
}

constexpr size_t maxElementNumber = 1024 * 64;
constexpr size_t threadsCount     = 128;
constexpr size_t modifyActions    = 2048;

TEST(mt_vault, allocation)
{
    auto                                   v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::array<std::jthread, threadsCount> thr;

    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < maxElementNumber / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    size_t sum = std::count_if(v->begin( ), v->end( ), [] (auto d) { return true; });
    EXPECT_EQ(sum, maxElementNumber);
}

TEST(mt_vault, modification)
{
    auto                                   v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::array<std::jthread, threadsCount> thr;

    // concurrent fill in 8 threads
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < maxElementNumber / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    // concurrent modify in 8 threads (multi-fields modify)
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([&v, i] ( ) {
            std::random_device                    dev;
            std::mt19937                          rng(dev( ));
            std::uniform_int_distribution<size_t> dist(0, maxElementNumber - 1);
            for ( size_t k = 0; k < modifyActions; k++ ) {
                size_t idx = dist(rng);
                auto   view {v->view(idx)};
                if ( view ) {
                    view( ).field_1++;
                    view( ).field_3.assign(fmt::format("{}_{}", view( ).field_3, i + 1));
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    size_t sum = std::accumulate(v->begin( ), v->end( ), 0, [] (size_t a, auto d) -> size_t { return a + d( ).field_1; });
    EXPECT_EQ(sum, threadsCount * modifyActions);
}

TEST(mt_vault, deallocation_by_index)
{
    auto                                   v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::array<std::jthread, threadsCount> thr;

    // concurrent fill in 8 threads
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < maxElementNumber / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    // concurrent deallocate in 8 threads (will deallocate already deallocated also to test collisions)
    std::atomic_size_t deallocationsCount {0};
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([&v, i, &deallocationsCount] ( ) {
            for ( size_t idx = i; idx < maxElementNumber; idx += 2 ) {
                if ( v->deallocate(idx) )
                    deallocationsCount.fetch_add(1);
                long_lasting_op( );
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    EXPECT_EQ(deallocationsCount, maxElementNumber);
    size_t sum = std::count_if(v->begin( ), v->end( ), [] (auto d) { return true; });
    EXPECT_EQ(sum, 0);
}

TEST(mt_vault, deallocation_by_predicate)
{
    auto                                   v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::array<std::jthread, threadsCount> thr;

    // concurrent fill in 8 threads
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < v->capacity( ) / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    // concurrent deallocate in 8 threads by same predicate (high collisions rate)
    std::atomic_size_t deallocationsCount {0};
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([&v, &deallocationsCount] ( ) {
            auto prefix_pred = [] (const Data& d) { return d.field_3.starts_with("2_"); };
            while ( v->deallocate(prefix_pred) ) {
                deallocationsCount.fetch_add(1);
                long_lasting_op( );
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    EXPECT_EQ(deallocationsCount, v->capacity( ) / threadsCount);
    size_t sum = std::count_if(v->begin( ), v->end( ), [] (auto d) { return true; });
    EXPECT_EQ(sum, v->capacity( ) - deallocationsCount.load( ));
}

TEST(mt_vault, allocate_into_sparse)
{
    auto                                   v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::array<std::jthread, threadsCount> thr;

    // concurrent fill in 8 threads
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < maxElementNumber / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    // concurrent deallocate in 8 threads by same predicate (high collisions rate)
    std::atomic_size_t deallocationsCount {0};
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([&v, &deallocationsCount] ( ) {
            auto prefix_pred = [] (const Data& d) { return d.field_3.starts_with("2_"); };
            while ( v->deallocate(prefix_pred) ) {
                deallocationsCount.fetch_add(1);
                long_lasting_op( );
            }
        });
    }
    for ( auto& t: thr )
        t.join( );
    // concurrent fill in 8 threads into sparse storage
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([&v, i, &deallocationsCount] ( ) {
            for ( size_t n = 0; n < deallocationsCount / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("additional {}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    //    v.dump();

    //    std::for_each(v.begin(), v.end(), [](auto view){ fmt::print("   {}\n", fmt::streamed(view()));});
    EXPECT_EQ(std::count_if(v->begin( ), v->end( ), [] (auto) { return true; }), v->capacity( ));
    EXPECT_EQ(std::count_if(v->begin( ), v->end( ), [] (auto view) { return view( ).field_3.starts_with("add"); }), deallocationsCount.load( ));
    EXPECT_EQ(std::count_if(v->begin( ), v->end( ), [] (auto view) { return !view( ).field_3.starts_with("add"); }), v->capacity( ) - deallocationsCount.load( ));
}

TEST(mt_vault, allocate_dealloacate)
{
    auto                                   v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::array<std::jthread, threadsCount> thr;

    // concurrent fill in 8 threads
    for ( size_t i = 0; i < threadsCount; i++ ) {
        thr[i] = std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < maxElementNumber / threadsCount; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        });
    }
    for ( auto& t: thr )
        t.join( );

    std::atomic_size_t deallocationsCount {0};
    for ( size_t i = 0; i < threadsCount / 2; i++ ) {
        thr[i] = std::jthread([&v, &deallocationsCount] ( ) {
            auto prefix_pred = [] (const Data& d) { return d.field_3.starts_with("4_"); };
            while ( v->deallocate(prefix_pred) ) {
                deallocationsCount.fetch_add(1);
                long_lasting_op( );
            }
        });
    }
    std::atomic_size_t allocationsCount {0};
    for ( size_t i = threadsCount / 2; i < threadsCount; i++ ) {
        thr[i] = std::jthread {[&v, &allocationsCount, &i] ( ) {
            for ( size_t n = 0; n < 2 * maxElementNumber / threadsCount / threadsCount; n++ ) {
                do {
                    if ( auto [view, inserted] = v->allocate( ); inserted ) {
                        view( ).field_3.assign(fmt::format("concurrent {}_{}", i + 1, n + 1));
                        view( ).field_1 = 0;
                        allocationsCount.fetch_add(1);
                        long_lasting_op( );
                        break;
                    }
                } while ( true );
            }
        }};
    }
    for ( auto& t: thr )
        t.join( );

    EXPECT_EQ(std::count_if(v->begin( ), v->end( ), [] (auto) { return true; }), maxElementNumber);
    EXPECT_EQ(deallocationsCount, maxElementNumber / threadsCount);
    EXPECT_EQ(allocationsCount, deallocationsCount);
    EXPECT_EQ(std::count_if(v->begin( ), v->end( ), [] (auto view) { return view( ).field_3.starts_with("conc"); }), maxElementNumber / threadsCount);
}

TEST(mt_vault, wild)
{
    auto                      v = std::make_unique<Vault<Data, maxElementNumber>>( );
    std::vector<std::jthread> thr;

    for ( size_t i = 0; i < threadsCount / 4; i++ ) {
        thr.push_back(std::jthread([i, &v] ( ) {
            for ( size_t n = 0; n < modifyActions; n++ ) {
                if ( auto [view, inserted] = v->allocate( ); inserted ) {
                    view( ).field_3.assign(fmt::format("{}_{}", i + 1, n + 1));
                    view( ).field_1 = 0;
                    long_lasting_op( );
                }
            }
        }));
    }
    for ( size_t i = 0; i < threadsCount / 4; i++ ) {
        thr.push_back(std::jthread {[i, &v] ( ) {
            std::random_device                    dev;
            std::mt19937                          rng(dev( ));
            std::uniform_int_distribution<size_t> dist(0, v->capacity( ) - 1);
            for ( size_t n = 0; n < modifyActions; n++ ) {
                v->deallocate(dist(rng));
                long_lasting_op( );
            }
        }});
    }
    for ( size_t i = 0; i < threadsCount / 4; i++ ) {
        thr.push_back(std::jthread {[i, &v] ( ) {
            std::random_device                    dev;
            std::mt19937                          rng(dev( ));
            std::uniform_int_distribution<size_t> dist1(0, v->capacity( ));
            std::uniform_int_distribution<size_t> dist2(1, modifyActions);
            for ( size_t n = 0; n < modifyActions; n++ ) {
                auto prefix_pred = [&dist1, &dist2, &rng] (const Data& d) { return d.field_3.starts_with(fmt::format("{}_{}", dist1(rng), dist2(rng))); };
                v->deallocate(prefix_pred);
                long_lasting_op( );
            }
        }});
    }
    for ( size_t i = 0; i < threadsCount / 4; i++ ) {
        thr.push_back(std::jthread {[&v, i] ( ) {
            std::random_device                    dev;
            std::mt19937                          rng(dev( ));
            std::uniform_int_distribution<size_t> dist(0, maxElementNumber - 1);
            for ( size_t k = 0; k < modifyActions; k++ ) {
                size_t idx = dist(rng);
                auto   view {v->view(idx)};
                if ( view ) {
                    view( ).field_1++;
                    view( ).field_3.assign(fmt::format("{}_{}", view( ).field_3, i + 1));
                    long_lasting_op( );
                }
            }
        }});
    }

    for ( auto& t: thr )
        t.join( );
}
