#include <benchmark/benchmark.h>
#include <spsc_ring_buffer.hpp>
#include <aligned_alloc.hpp>
#include <chrono>
#include <thread>
#include <new>

constexpr int buffer_size_log2 = 16;
constexpr int content_align_log2 = 3;

void configure_benchmark(benchmark::internal::Benchmark* bench) {
    bench->ArgNames({"Count", "Size"});

    bench->Args({ 1, 8 });
    bench->Args({ 10, 8 });
    bench->Args({ 100, 8 });
    bench->Args({ 1000, 8 });
    bench->Args({ 10000, 8 });
    bench->Args({ 100000, 8 });

    bench->Args({ 100, 16 });
    bench->Args({ 1000, 16 });
    bench->Args({ 10000, 16 });

    bench->Args({ 100, 24 });
    bench->Args({ 1000, 24 });
    bench->Args({ 10000, 24 });

    bench->Args({ 100, 56 });
    bench->Args({ 1000, 56 });
    bench->Args({ 10000, 56 });

    bench->Args({ 100, 120 });
    bench->Args({ 1000, 120 });
    bench->Args({ 10000, 120 });

    bench->Args({ 100, 184 });
    bench->Args({ 1000, 184 });
    bench->Args({ 10000, 184 });

    bench->Args({ 100, 248 });
    bench->Args({ 1000, 248 });
    bench->Args({ 10000, 248 });
}

template<typename type>
static void RingBuffer(benchmark::State& state) {
    static auto* buffer = new(aligned_alloc(type::align, sizeof(type))) type{};

    auto& b = *buffer;
    size_t calls = 0;
    if (state.thread_index == 0) {        
        for (auto _ : state) {
            int counter = 0;
            while (counter < state.range(0)) {
                bool result = b.produce(state.range(1), [](void*) { return true; });
                counter += int(result);
                //calls += 1;
                //if (result == false) {
                //    state.PauseTiming();
                //    _sleep(0);
                //    //std::this_thread::sleep_for(std::chrono::seconds(0));
                //    state.ResumeTiming();
                //}
            }
            
        }
        //state.counters["produce_calls"].value += calls - state.iterations() * state.range(0);
        state.SetItemsProcessed(state.iterations() * state.range(0));
        state.SetBytesProcessed(state.iterations() * state.range(0) * state.range(1));
    } else {
        for (auto _ : state) {
            int counter = 0;
            while (counter < state.range(0)) {
                bool result = b.consume([](const void*, ptrdiff_t) { return true; });
                counter += int(result);
                //calls += 1;
                //if (result == false) {
                //    state.PauseTiming();
                //    _sleep(0);
                //    //std::this_thread::sleep_for(std::chrono::milliseconds(0));
                //    state.ResumeTiming();
                //}
            }
        }
        //state.counters["consume_calls"].value += calls - state.iterations() * state.range(0);
        //state.SetItemsProcessed(state.iterations() * state.range(0));
    }

    if (b.is_empty() == false) {
        state.SkipWithError("Not Empty after test");
    }
    
}

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<16>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<16>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<16>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<18>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<18>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<18>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<22>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<22>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<22>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer<30>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_2<30>)->Threads(2)->Apply(configure_benchmark);
BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<30>)->Threads(2)->Apply(configure_benchmark);

BENCHMARK_TEMPLATE(RingBuffer, spsc_ring_buffer_3<32>)->Threads(2)->Apply(configure_benchmark);

BENCHMARK_MAIN();
