# Ruby's Integer timings for the operations in tests/bench_ops.c (same sizes,
# same output format), to compare with tools/compare_ops.py.
#
#     ruby tests/bench_ops.rb [quick]        prints:  <op> <limbs> <ns per op>

MIN_TIME = 0.2
SIZES = [1, 4, 16, 64, 256, 1024, 4096, 16384]
MAX = ARGV[0] == "quick" ? 1024 : 16384
RNG = Random.new(42)

def clock = Process.clock_gettime(Process::CLOCK_MONOTONIC)

def random_bits(n) = RNG.rand(1 << (64 * n)) | (1 << (64 * n - 1))

def bench(label, n)
  reps = 1
  loop do
    t0 = clock
    i = 0
    while i < reps
      yield
      i += 1
    end
    t = clock - t0
    if t > MIN_TIME
      puts format("%s %d %.1f", label, n, t / reps * 1e9)
      $stdout.flush
      return
    end
    reps *= 2
  end
end

sizes = SIZES.select { |n| n <= MAX }

sizes.each { |n| a = random_bits(n); b = random_bits(n); bench("mul", n) { a * b } }
sizes.each { |n| a = random_bits(n); bench("sqr", n) { a * a } }
sizes.each { |n| a = random_bits(2 * n); bench("sqrt", n) { Integer.sqrt(a) } }
sizes.each { |n| a = random_bits(2 * n); b = random_bits(n); bench("div", n) { a.divmod(b) } }
sizes.each do |n|
  a = random_bits(n)
  s = a.to_s
  bench("get_str", n) { a.to_s }
  bench("set_str", n) { s.to_i }
end
