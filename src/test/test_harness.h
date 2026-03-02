#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestResult {
  std::string name;
  bool passed;
  std::string message;
};

struct TestRegistry {
  struct TestEntry {
    std::string name;
    std::function<void()> func;
  };

  static std::vector<TestEntry>& tests() {
    static std::vector<TestEntry> entries;
    return entries;
  }

  static std::vector<TestResult>& results() {
    static std::vector<TestResult> res;
    return res;
  }

  static std::string& current_test() {
    static std::string name;
    return name;
  }

  static bool& current_passed() {
    static bool p = true;
    return p;
  }

  static int register_test(const char* name, std::function<void()> func) {
    tests().push_back({name, func});
    return 0;
  }

  static int run_all() {
    int passed = 0, failed = 0;

    printf("{\"tests\":[\n");
    bool first = true;

    for (auto& t : tests()) {
      current_test() = t.name;
      current_passed() = true;
      std::string msg;

      try {
        t.func();
      } catch (const std::exception& e) {
        current_passed() = false;
        msg = e.what();
      }

      if (current_passed()) {
        passed++;
      } else {
        failed++;
      }

      if (!first) printf(",\n");
      first = false;

      printf("  {\"name\":\"%s\",\"passed\":%s", t.name.c_str(),
             current_passed() ? "true" : "false");
      if (!msg.empty()) {
        printf(",\"message\":\"%s\"", msg.c_str());
      }
      printf("}");

      results().push_back({t.name, current_passed(), msg});
    }

    printf("\n],\"summary\":{\"total\":%d,\"passed\":%d,\"failed\":%d}}\n",
           passed + failed, passed, failed);

    return failed > 0 ? 1 : 0;
  }
};

#define DELVE_TEST(name)                                                     \
  static void test_##name();                                                 \
  static int _reg_##name = TestRegistry::register_test(#name, test_##name);  \
  static void test_##name()

#define EXPECT_TRUE(cond)                                                    \
  do {                                                                       \
    if (!(cond)) {                                                           \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_TRUE(%s)\n", __FILE__, __LINE__, \
              #cond);                                                        \
    }                                                                        \
  } while (0)

#define EXPECT_FALSE(cond)                                                   \
  do {                                                                       \
    if ((cond)) {                                                            \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_FALSE(%s)\n", __FILE__,          \
              __LINE__, #cond);                                              \
    }                                                                        \
  } while (0)

#define EXPECT_NEAR(actual, expected, epsilon)                               \
  do {                                                                       \
    auto _a = (actual);                                                      \
    auto _e = (expected);                                                    \
    auto _eps = (epsilon);                                                   \
    if (std::abs(_a - _e) > _eps) {                                          \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_NEAR(%s, %s, %s) — got %g, "    \
              "expected %g (diff %g)\n", __FILE__, __LINE__, #actual,        \
              #expected, #epsilon, (double)_a, (double)_e,                   \
              (double)std::abs(_a - _e));                                    \
    }                                                                        \
  } while (0)

#define EXPECT_RANGE(value, min_val, max_val)                                \
  do {                                                                       \
    auto _v = (value);                                                       \
    auto _min = (min_val);                                                   \
    auto _max = (max_val);                                                   \
    if (_v < _min || _v > _max) {                                            \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_RANGE(%s, %s, %s) — got %g, "   \
              "range [%g, %g]\n", __FILE__, __LINE__, #value, #min_val,      \
              #max_val, (double)_v, (double)_min, (double)_max);             \
    }                                                                        \
  } while (0)

#define EXPECT_GT(a, b)                                                      \
  do {                                                                       \
    auto _a = (a);                                                           \
    auto _b = (b);                                                           \
    if (!(_a > _b)) {                                                        \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_GT(%s, %s) — %g <= %g\n",       \
              __FILE__, __LINE__, #a, #b, (double)_a, (double)_b);           \
    }                                                                        \
  } while (0)

#define EXPECT_LT(a, b)                                                      \
  do {                                                                       \
    auto _a = (a);                                                           \
    auto _b = (b);                                                           \
    if (!(_a < _b)) {                                                        \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_LT(%s, %s) — %g >= %g\n",       \
              __FILE__, __LINE__, #a, #b, (double)_a, (double)_b);           \
    }                                                                        \
  } while (0)

#define EXPECT_EQ(a, b)                                                      \
  do {                                                                       \
    auto _a = (a);                                                           \
    auto _b = (b);                                                           \
    if (!(_a == _b)) {                                                       \
      TestRegistry::current_passed() = false;                                \
      fprintf(stderr, "  FAIL %s:%d: EXPECT_EQ(%s, %s)\n",                  \
              __FILE__, __LINE__, #a, #b);                                   \
    }                                                                        \
  } while (0)
