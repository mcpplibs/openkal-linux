// What openkal-linux 0.22 changes beside openkal 0.21, as this kernel does it.
//
// A CHUNK IS RESIDENT ONLY WHERE ITS BLOCKS WERE HANDED OUT: one block of each
// of the twelve classes makes far less than a mebibyte resident, where a free
// list threaded through each chunk at once made twelve.
//
// AN ALIGNMENT WIDER THAN A PAGE IS HONOURED for a size a class would hold: a
// block of a class is aligned to its size or to the page, whichever is less.
import openkal.memory;
import openkal.process;
import openkal.stream;
import openkal.types;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
    if (ok) return;
    ++failures;
    const char pre[] = "FAIL: ";
    kal::write(kal::err(), pre, sizeof(pre) - 1);
    kal_uintptr n = 0; while (what[n]) ++n;
    kal::write(kal::err(), what, n);
    kal::write(kal::err(), "\n", 1);
}

kal_u64 resident() {
    kal_process self{};
    if (kal_process_self(&self) != kal_ok) return 0;
    kal_program_info info{};
    info.self_size = sizeof info;
    const int e = kal_process_info(self, kal::process::info::memory, &info);
    kal_process_close(self);
    return e == kal_ok && (info.present & kal::process::info::memory) ? info.memory_bytes : 0;
}
}  // namespace

int main() {
    {
        void* blocks[12] = {};
        const kal_u64 before = resident();
        kal_uintptr size = 16;
        for (auto& b : blocks) { b = kal_alloc(size, 16); size <<= 1; }
        const kal_u64 after = resident();
        bool all = true;
        for (auto* b : blocks) all = all && b != nullptr;
        check(all, "a block of each class is allocated");
        check(before != 0 && after < before + (kal_u64{1} << 20),
              "one block of each class makes less than a mebibyte resident");
        size = 16;
        for (auto* b : blocks) { kal_free(b, size, 16); size <<= 1; }
    }
    {
        const kal_uintptr aligns[] = { 8192, 16384, 32768 };
        for (const kal_uintptr a : aligns) {
            void* p = kal_alloc(100, a);
            check(p != nullptr && reinterpret_cast<kal_uintptr>(p) % a == 0,
                  "a small allocation is aligned to an alignment wider than the page");
            kal_free(p, 100, a);
        }
    }
    return failures == 0 ? 0 : 1;
}
