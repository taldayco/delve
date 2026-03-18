#pragma once
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase {
  std::string name;
  std::function<bool()> fn;
};

class TestRegistry {
public:
  static std::vector<TestCase> &cases() {
    static std::vector<TestCase> c;
    return c;
  }

  static int add(const char *name, std::function<bool()> fn) {
    cases().push_back({name, std::move(fn)});
    return 0;
  }

  static int run_all() {
    int passed = 0, failed = 0;
    std::vector<std::string> failures;

    printf("{\"tests\":[\n");
    bool first = true;
    for (auto &tc : cases()) {
      if (!first) printf(",\n");
      first = false;

      bool ok = false;
      try { ok = tc.fn(); } catch (...) { ok = false; }

      if (ok) ++passed;
      else { ++failed; failures.push_back(tc.name); }

      printf("  {\"name\":\"%s\",\"result\":\"%s\"}", tc.name.c_str(), ok ? "pass" : "FAIL");
    }
    printf("\n],\n\"passed\":%d,\"failed\":%d,\"total\":%d}\n",
           passed, failed, passed + failed);
    return failed > 0 ? 1 : 0;
  }
};

#define DELVE_TEST(name)                                                       \
  static bool test_fn_##name();                                                \
  static int reg_##name = TestRegistry::add(#name, test_fn_##name);            \
  static bool test_fn_##name()

#define EXPECT_TRUE(expr)                                                      \
  do { if (!(expr)) {                                                          \
    fprintf(stderr, "  FAIL: %s:%d: %s\n", __FILE__, __LINE__, #expr);         \
    return false;                                                              \
  }} while(0)

#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))

#define EXPECT_NEAR(a, b, eps)                                                 \
  do { if (std::abs((a) - (b)) > (eps)) {                                      \
    fprintf(stderr, "  FAIL: %s:%d: |%f - %f| > %f\n", __FILE__, __LINE__,    \
            (double)(a), (double)(b), (double)(eps));                           \
    return false;                                                              \
  }} while(0)

#define EXPECT_RANGE(val, lo, hi)                                              \
  do { auto v_ = (val); if (v_ < (lo) || v_ > (hi)) {                         \
    fprintf(stderr, "  FAIL: %s:%d: %f not in [%f, %f]\n", __FILE__, __LINE__,\
            (double)v_, (double)(lo), (double)(hi));                            \
    return false;                                                              \
  }} while(0)

#define EXPECT_GT(a, b)                                                        \
  do { if (!((a) > (b))) {                                                     \
    fprintf(stderr, "  FAIL: %s:%d: %f <= %f\n", __FILE__, __LINE__,          \
            (double)(a), (double)(b));                                          \
    return false;                                                              \
  }} while(0)

#define EXPECT_LT(a, b)                                                        \
  do { if (!((a) < (b))) {                                                     \
    fprintf(stderr, "  FAIL: %s:%d: %f >= %f\n", __FILE__, __LINE__,          \
            (double)(a), (double)(b));                                          \
    return false;                                                              \
  }} while(0)

#define EXPECT_GE(a, b)                                                        \
  do { if (!((a) >= (b))) {                                                     \
    fprintf(stderr, "  FAIL: %s:%d: %f < %f\n", __FILE__, __LINE__,           \
            (double)(a), (double)(b));                                          \
    return false;                                                              \
  }} while(0)

#define EXPECT_EQ(a, b)                                                        \
  do { if ((a) != (b)) {                                                       \
    fprintf(stderr, "  FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
    return false;                                                              \
  }} while(0)
