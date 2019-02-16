#include "cpuid.hpp"
#include "bitfield.hpp"
#include <iostream>
#include <thread>

#if defined(_MSC_VER)
#include <Windows.h>
#include <powerbase.h>
#include <intrin.h>
#elif defined(__GNUC__)
#include <x86intrin.h>
#include <cpuid.h>
#endif

static void cpuid(int(&output)[4], int leaf) {
#if defined(_MSC_VER)
    __cpuid(output, leaf);
#elif defined(__GNUC__)
    __get_cpuid(leaf, output + 0, output + 1, output + 2, output + 3);
#else
#error "unsupported compiler (find out how to invoke CPUID for this one)"
#endif
}

static void cpuidex(int(&output)[4], int leaf, int subleaf) {
#if defined(_MSC_VER)
    __cpuidex(output, leaf, subleaf);
#elif defined(__GNUC__)
    __get_cpuid_count(leaf, subleaf, output + 0, output + 1, output + 2, output + 3);
#else
#error "unsupported compiler (find out how to invoke CPUID with subleaf for this one)"
#endif
}

static u64 tsc_frequency_;
void measure_tsc_frequency() {
    int output[4];
    cpuid(output, 0);
    auto max_function = output[0];
    if (max_function >= 0x16) {
        struct cpuid_frequency_info {
            u32 base_frequency;
            u32 maximum_frequency;
            u32 bus_frequency;
            u32 _unused;
        };

        cpuid_frequency_info info;
        cpuid(output, 0x16);
        memcpy(&info, output, sizeof(info));

        tsc_frequency_ = info.base_frequency * 1'000'000;
    } else {
#if defined(_MSC_VER)
        typedef struct _PROCESSOR_POWER_INFORMATION {
            ULONG Number;
            ULONG MaxMhz;
            ULONG CurrentMhz;
            ULONG MhzLimit;
            ULONG MaxIdleState;
            ULONG CurrentIdleState;
        } PROCESSOR_POWER_INFORMATION, *PPROCESSOR_POWER_INFORMATION;

        SYSTEM_INFO info;
        GetSystemInfo(&info);
        std::vector<PROCESSOR_POWER_INFORMATION> core_info(info.dwNumberOfProcessors);
        CallNtPowerInformation(ProcessorInformation, nullptr, 0, core_info.data(), info.dwNumberOfProcessors * sizeof(PROCESSOR_POWER_INFORMATION));

        tsc_frequency_ = core_info[0].MaxMhz * 1'000'000;
#else
        unsigned cpu;
        auto start = __rdtscp(&cpu);
        std::this_thread::sleep_for(std::chrono::seconds{ 1 });
        auto end = __rdtscp(&cpu);

        auto clockrate = end - start;

        constexpr auto clock_accuracy = u64(4'000'000);

        tsc_frequency_ = ((clockrate + clock_accuracy / 2) / clock_accuracy) * clock_accuracy;
#endif
    }
}

u64 tsc_frequency() {
    return tsc_frequency_;
}
u64 tsc() {
    return __rdtsc();
}

struct cpuid_leaf1 {

    union additional_information_t {
        u32 all_bits;
        bitfield<u32, 0, 7> brand_index;
        bitfield<u32, 8, 15> clflush_size;
        bitfield<u32, 16, 23> id_count;
        bitfield<u32, 24, 31> apic_id;
    };

    union processor_signature_t {
        u32 all_bits;
        bitfield<u32, 0, 3> stepping_id;
        bitfield<u32, 4, 7> model_id;
        bitfield<u32, 8, 11> family_id;
        bitfield<u32, 12, 13> processor_type;
        bitfield<u32, 16, 19> extended_model;
        bitfield<u32, 20, 27> extended_family;

        u32 family() {
            if (family_id < 0xF) {
                return family_id;
            }
            return family_id + extended_family;
        }

        u32 model() {
            u32 family = family_id;
            if (family == 0x6 || family == 0xF) {
                return (extended_model << 4) | model_id;
            }
            return model_id;
        }

        u32 stepping() {
            return stepping_id;
        }
    };

    union feature_information_t {
        u64 all_bits;
        bitfield<u64, 0> sse3;
        bitfield<u64, 1> pclmulqdq;
        bitfield<u64, 2> dtes64;
        bitfield<u64, 3> monitor;
        bitfield<u64, 4> ds_cpl;
        bitfield<u64, 5> vmx;
        bitfield<u64, 6> smx;
        bitfield<u64, 7> eist;
        bitfield<u64, 8> tm2;
        bitfield<u64, 9> ssse3;
        bitfield<u64, 10> cnxt_id;
        bitfield<u64, 11> sdbg;
        bitfield<u64, 12> fma;
        bitfield<u64, 13> cmpxchg16b;
        bitfield<u64, 14> xtpr_update_control;
        bitfield<u64, 15> pdcm;
        //bitfield<u64, 16> reserved;
        bitfield<u64, 17> pcid;
        bitfield<u64, 18> dca;
        bitfield<u64, 19> sse4_1;
        bitfield<u64, 20> sse4_2;
        bitfield<u64, 21> x2apic;
        bitfield<u64, 22> movbe;
        bitfield<u64, 23> popcnt;
        bitfield<u64, 24> tsc_deadline;
        bitfield<u64, 25> aesni;
        bitfield<u64, 26> xsave;
        bitfield<u64, 27> osxsave;
        bitfield<u64, 28> avx;
        bitfield<u64, 29> f16c;
        bitfield<u64, 30> rdrand;
        //bitfield<u64, 31> not_used;
        bitfield<u64, 32> fpu;
        bitfield<u64, 33> vme;
        bitfield<u64, 34> de;
        bitfield<u64, 35> pse;
        bitfield<u64, 36> tsc;
        bitfield<u64, 37> msr;
        bitfield<u64, 38> pae;
        bitfield<u64, 39> mce;
        bitfield<u64, 40> cx8;
        bitfield<u64, 41> apic;
        //bitfield<u64, 42> reserved;
        bitfield<u64, 43> sep;
        bitfield<u64, 44> mtrr;
        bitfield<u64, 45> pge;
        bitfield<u64, 46> mca;
        bitfield<u64, 47> cmov;
        bitfield<u64, 48> pat;
        bitfield<u64, 49> pse_36;
        bitfield<u64, 50> psn;
        bitfield<u64, 51> clfsh;
        //bitfield<u64, 52> reserved;
        bitfield<u64, 53> ds;
        bitfield<u64, 54> acpi;
        bitfield<u64, 55> mmx;
        bitfield<u64, 56> fxsr;
        bitfield<u64, 57> sse;
        bitfield<u64, 58> sse2;
        bitfield<u64, 59> ss;
        bitfield<u64, 60> htt;
        bitfield<u64, 61> tm;
        //bitfield<u64, 62> reserved;
        bitfield<u64, 63> pbe;
    };

    processor_signature_t processor_signature;
    additional_information_t additional_information;
    feature_information_t feature_information;

};

const char lf = '\n';

struct cache_description {
    enum cache_type : u32 {
        null = 0u,
        data = 1u,
        instruction = 2u,
        unified = 3u
    };

    union eax {
        u32 all_bits;
        bitfield<u32, 0, 4> type;
        bitfield<u32, 5, 7> level;
        bitfield<u32, 8> self_initializing;
        bitfield<u32, 9> fully_associative;
        //bitfield<u32, 10, 13> reserved;
        bitfield<u32, 14, 25> share_count_logical;
        bitfield<u32, 26, 31> share_count_physical;
    };

    union ebx {
        u32 all_bits;
        bitfield<u32, 0, 11> line_size;
        bitfield<u32, 12, 21> partitions;
        bitfield<u32, 22, 31> associativity;
    };

    union ecx {
        u32 set_count;
    };

    union edx {
        u32 all_bits;
        bitfield<u32, 0> forward_invalidate;
        bitfield<u32, 1> inclusive;
        bitfield<u32, 2> complex_indexing;
    };

    eax eax_;
    ebx ebx_;
    ecx ecx_;
    edx edx_;

    u32 level() {
        return eax_.level;
    }

    cache_type type() {
        return static_cast<cache_type>(static_cast<u32>(eax_.type));
    }

    u32 size() {
        return line_size() *
            (ebx_.partitions + 1) *
            (ebx_.associativity + 1) *
            (ecx_.set_count + 1);
    }

    u32 line_size() {
        return ebx_.line_size + 1;
    }

};

void analyze() {
    int output[4];
    cpuid(output, 0);
    auto max_function = output[0];
    std::cout << "Max Function: " << max_function << lf;

    cpuid(output, 1);

    cpuid_leaf1 leaf1;
    memcpy(&leaf1, output, sizeof(leaf1));

    std::cout << "Family:   " << leaf1.processor_signature.family() << lf;
    std::cout << "Model:    " << leaf1.processor_signature.model() << lf;
    std::cout << "Stepping: " << leaf1.processor_signature.stepping() << lf;

    int index = 0;
    cpuidex(output, 4, index);
    cache_description inst;
    memcpy(&inst, output, sizeof(inst));
    while (inst.type() != cache_description::null) {
        std::cout << "L" << inst.level() << (inst.type() == cache_description::data ? "D" : (inst.type() == cache_description::instruction ? "I" : "")) << ":" << lf
            << "  Size: " << inst.size() << lf
            << "  Line Size: " << inst.line_size() << lf;

        index += 1;
        cpuidex(output, 4, index);
        memcpy(&inst, output, sizeof(inst));
    }
}
