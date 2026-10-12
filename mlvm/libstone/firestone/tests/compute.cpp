#include "Test.hpp"
#include <cstring>
#include <map>
#include <memory>
#include <numeric>

using namespace firestone::compute;
struct DeviceState {
    NativeHandle next = 1;
    std::map<NativeHandle, std::vector<std::byte>> buffers;
    std::map<NativeHandle, std::string> kernels;
    unsigned launches = 0, releases = 0, syncs = 0;
};
BackendFunctions adapter(const std::shared_ptr<DeviceState>& state) {
    BackendFunctions functions; functions.device.name = "test synchronous adapter";
    functions.device.max_allocation = 1024;
    functions.allocate = [state](std::size_t bytes) { auto handle = state->next++; state->buffers.emplace(handle, std::vector<std::byte>(bytes)); return handle; };
    functions.release_buffer = [state](NativeHandle handle) { state->buffers.erase(handle); ++state->releases; };
    functions.write = [state](NativeHandle handle, std::size_t offset, const void* data, std::size_t bytes) {
        std::memcpy(state->buffers.at(handle).data() + offset, data, bytes);
    };
    functions.read = [state](NativeHandle handle, std::size_t offset, void* data, std::size_t bytes) {
        std::memcpy(data, state->buffers.at(handle).data() + offset, bytes);
    };
    functions.compile = [state](const std::string& source, const std::string& entry, const std::vector<std::string>& options) {
        CHECK(source == "test add kernel" && options == std::vector<std::string>{"checked"});
        auto handle = state->next++; state->kernels.emplace(handle, entry); return handle;
    };
    functions.release_kernel = [state](NativeHandle handle) { state->kernels.erase(handle); ++state->releases; };
    functions.launch = [state](NativeHandle kernel, const NDRange& range, const std::vector<NativeArgument>& args) {
        CHECK(state->kernels.at(kernel) == "add" && args.size() == 2);
        CHECK(args[0].kind == NativeArgument::Kind::Buffer && args[1].kind == NativeArgument::Kind::Scalar);
        int increment; std::memcpy(&increment, args[1].scalar.data(), sizeof(increment));
        auto& buffer = state->buffers.at(args[0].buffer);
        CHECK(range.global[0] * sizeof(int) <= buffer.size());
        for (std::size_t i = 0; i < range.global[0]; ++i) {
            int value; std::memcpy(&value, buffer.data() + i * sizeof(int), sizeof(value)); value += increment;
            std::memcpy(buffer.data() + i * sizeof(int), &value, sizeof(value));
        }
        ++state->launches;
    };
    functions.synchronize = [state] { ++state->syncs; };
    return functions;
}
template<class Device> void exercise() {
    Device unavailable; CHECK(!unavailable.available());
    throws<Unavailable>([&] { (void)unavailable.allocate(16); });
    throws<std::invalid_argument>([] { Device device(BackendFunctions{}); });
    auto state = std::make_shared<DeviceState>();
    Buffer survivor;
    {
        Device device(adapter(state)); CHECK(device.available());
        int input[]{1, 2, 3, 4}, output[4]{};
        auto buffer = device.allocate(sizeof(input)); buffer.write(input, sizeof(input));
        auto kernel = device.compile("test add kernel", "add", {"checked"});
        NDRange range{{4, 1, 1}, {2, 1, 1}};
        device.launch(kernel, range, {Argument::buffer(buffer), Argument::scalar(7)});
        device.synchronize(); buffer.read(output, sizeof(output));
        CHECK(output[0] == 8 && output[3] == 11 && state->launches == 1 && state->syncs == 1);
        throws<std::out_of_range>([&] { buffer.read(output, 8, sizeof(input) - 4); });
        throws<std::invalid_argument>([&] { buffer.write(nullptr, 4); });
        buffer.write(nullptr, 0, buffer.size());
        throws<std::out_of_range>([&] { device.allocate(1025); });
        throws<std::out_of_range>([&] { device.allocate(0); });
        auto bad = range; bad.local[0] = 3;
        throws<std::invalid_argument>([&] { device.launch(kernel, bad); });
        bad = range; bad.global[0] = bad.local[0] = 2048;
        throws<std::out_of_range>([&] { device.launch(kernel, bad); });
        Device foreign(adapter(state)); auto other = foreign.allocate(16);
        throws<std::invalid_argument>([&] { device.launch(kernel, range, {Argument::buffer(other)}); });
        throws<std::invalid_argument>([&] { foreign.launch(kernel, range); });
        throws<std::invalid_argument>([] { (void)Argument::local(0); });
        auto copy = device; copy.launch(kernel, range, {Argument::buffer(buffer), Argument::scalar(1)});
        survivor = buffer;
    }
    CHECK(state->kernels.empty() && state->buffers.size() == 1);
    int first = 0; survivor.read(&first, sizeof(first)); CHECK(first == 9);
    survivor = {}; CHECK(state->buffers.empty() && state->releases == 3);
    auto failed = adapter(state); failed.allocate = [](std::size_t) -> NativeHandle { return 0; };
    Device failing(std::move(failed)); throws<std::runtime_error>([&] { failing.allocate(4); });
}
int main() { return test_main([] {
    exercise<CUDA>(); exercise<HIP>(); exercise<OpenCL>(); exercise<SYCL>();
    std::vector<int> values(1000);
    OpenMP::parallel_for(values.size(), [&](std::size_t i) { values[i] = static_cast<int>(i); });
    CHECK(std::accumulate(values.begin(), values.end(), 0) == 499500);
    CHECK(OpenMP::parallel_reduce(1000, 0, [](std::size_t i) { return static_cast<int>(i); }, [](int a, int b) { return a + b; }) == 499500);
    CHECK(OpenMP::parallel_reduce(4, std::string(), [](std::size_t i) { return std::to_string(i); }, [](std::string a, std::string b) { return a + b; }) == "0123");
    throws<std::runtime_error>([] { OpenMP::parallel_for(100, [](std::size_t i) { if (i == 7) throw std::runtime_error("map failure"); }); });
    CHECK(OpenMP::max_threads() >= 1);
}); }
