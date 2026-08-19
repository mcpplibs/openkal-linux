import openkal.stream;
import openkal.memory;

int main() {
    const char greeting[] = "openkal: hello\n";
    kal::write(kal::out(), greeting, sizeof(greeting) - 1);

    void* region = kal::alloc(64, 8);
    const char* report = region != nullptr ? "openkal: allocation succeeded\n"
                                           : "openkal: allocation failed\n";
    kal_uintptr n = 0; while (report[n] != '\0') ++n;
    kal::write(kal::out(), report, n);
    kal::free(region, 64, 8);

    // A capability the implementation does not provide is detected at compile
    // time, so the program selects an alternative without a run-time test.
    if constexpr (kal::has_write_vectored<kal::stream>) {
        const char v[] = "openkal: vectored writes available\n";
        kal::write(kal::out(), v, sizeof(v) - 1);
    } else {
        const char v[] = "openkal: vectored writes unavailable\n";
        kal::write(kal::out(), v, sizeof(v) - 1);
    }
    return 0;
}
