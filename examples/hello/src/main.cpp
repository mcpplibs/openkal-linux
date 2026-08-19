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

    // The program imports openkal and names no implementation. Which
    // implementation supplies the definitions is decided in the manifest.
    const char done[] = "openkal: the program named no implementation\n";
    kal::write(kal::out(), done, sizeof(done) - 1);
    return 0;
}
