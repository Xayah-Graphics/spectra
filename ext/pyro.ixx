export module pyro;

namespace xayah {
    export class PyroSolver {
    public:
        PyroSolver()           = default;
        ~PyroSolver() noexcept = default;

        PyroSolver(const PyroSolver& other)                = delete;
        PyroSolver(PyroSolver&& other) noexcept            = default;
        PyroSolver& operator=(const PyroSolver& other)     = delete;
        PyroSolver& operator=(PyroSolver&& other) noexcept = default;

    private:
        struct {

        } host;

        struct {

        } device;
    };
} // namespace xayah
