#!/usr/bin/env python3
"""Unit tests for NoexceptEsp32Checker."""

import unittest

from ci.lint_cpp.noexcept_esp32_checker import NoexceptEsp32Checker
from ci.util.check_files import FileContent


_DRIVER_PATH = "src/platforms/esp/32/drivers/rmt/some_driver.h"
_DRIVER_CPP = "src/platforms/esp/32/drivers/spi/channel_driver_spi.cpp.hpp"


def _make(code: str, path: str = _DRIVER_PATH) -> FileContent:
    return FileContent(path=path, content=code, lines=code.splitlines())


def _violations(code: str, path: str = _DRIVER_PATH) -> list[tuple[int, str]]:
    c = NoexceptEsp32Checker()
    fc = _make(code, path)
    if not c.should_process_file(path):
        return []
    c.check_file_content(fc)
    return c.violations.get(path, [])


# ── File filtering ──────────────────────────────────────────────────────────


class TestFileFiltering(unittest.TestCase):
    def test_driver_header(self) -> None:
        self.assertTrue(NoexceptEsp32Checker().should_process_file(_DRIVER_PATH))

    def test_driver_cpp_hpp(self) -> None:
        self.assertTrue(NoexceptEsp32Checker().should_process_file(_DRIVER_CPP))

    def test_outside_drivers_skipped(self) -> None:
        self.assertFalse(
            NoexceptEsp32Checker().should_process_file("src/fl/system/log.h")
        )

    def test_esp32_non_driver_skipped(self) -> None:
        self.assertFalse(
            NoexceptEsp32Checker().should_process_file(
                "src/platforms/esp/32/mutex_esp32.h"
            )
        )


# ── Should flag (missing noexcept) ──────────────────────────────────────────


class TestMissingNoexcept(unittest.TestCase):
    def test_void_func(self) -> None:
        self.assertEqual(len(_violations("void foo();")), 1)

    def test_return_type(self) -> None:
        self.assertEqual(len(_violations("int bar(float x);")), 1)

    def test_const_method(self) -> None:
        self.assertEqual(len(_violations("bool isReady() const;")), 1)

    def test_definition(self) -> None:
        self.assertEqual(len(_violations("void foo() {")), 1)

    def test_class_method_def(self) -> None:
        self.assertEqual(len(_violations("void MyClass::doWork() {")), 1)

    def test_static_func(self) -> None:
        self.assertEqual(len(_violations("static void helper(int x);")), 1)

    def test_constructor(self) -> None:
        self.assertEqual(len(_violations("ChannelEngineSpi();")), 1)

    def test_multiple(self) -> None:
        self.assertEqual(len(_violations("void a();\nint b();\nbool c() const;")), 3)


# ── Should pass (has noexcept / FL_NOEXCEPT) ────────────────────────────────


class TestHasNoexcept(unittest.TestCase):
    def test_noexcept(self) -> None:
        self.assertEqual(len(_violations("void foo() noexcept;")), 0)

    def test_noexcept_def(self) -> None:
        self.assertEqual(len(_violations("void foo() noexcept {")), 0)

    def test_noexcept_const(self) -> None:
        self.assertEqual(len(_violations("bool bar() const noexcept;")), 0)

    def test_fl_noexcept(self) -> None:
        self.assertEqual(len(_violations("void foo() FL_NOEXCEPT;")), 0)

    def test_fl_noexcept_def(self) -> None:
        self.assertEqual(len(_violations("void foo() FL_NOEXCEPT {")), 0)

    def test_fl_noexcept_const(self) -> None:
        self.assertEqual(len(_violations("bool bar() const FL_NOEXCEPT;")), 0)

    def test_fl_noexcept_override(self) -> None:
        self.assertEqual(len(_violations("void run() FL_NOEXCEPT override;")), 0)

    def test_noexcept_override(self) -> None:
        self.assertEqual(len(_violations("void run() noexcept override;")), 0)


# ── Exemptions ──────────────────────────────────────────────────────────────


class TestExemptions(unittest.TestCase):
    def test_destructor(self) -> None:
        self.assertEqual(len(_violations("~ChannelEngineSpi();")), 0)

    def test_destructor_qualified(self) -> None:
        self.assertEqual(len(_violations("Foo::~Foo() {")), 0)

    def test_deleted(self) -> None:
        self.assertEqual(len(_violations("Foo(const Foo&) = delete;")), 0)

    def test_defaulted(self) -> None:
        self.assertEqual(len(_violations("Foo() = default;")), 0)

    def test_pure_virtual(self) -> None:
        self.assertEqual(len(_violations("virtual void process() = 0;")), 0)

    def test_comment(self) -> None:
        self.assertEqual(len(_violations("// void foo();")), 0)

    def test_multiline_comment(self) -> None:
        self.assertEqual(len(_violations("/* void foo();\n*/")), 0)

    def test_suppression(self) -> None:
        self.assertEqual(len(_violations("void foo(); // ok no noexcept")), 0)

    def test_macro_define(self) -> None:
        self.assertEqual(len(_violations("#define FOO(x) bar(x)")), 0)

    def test_if_statement(self) -> None:
        self.assertEqual(len(_violations("if (condition) {")), 0)

    def test_while_loop(self) -> None:
        self.assertEqual(len(_violations("while (running) {")), 0)

    def test_for_loop(self) -> None:
        self.assertEqual(len(_violations("for (int i = 0; i < n; i++) {")), 0)

    def test_switch(self) -> None:
        self.assertEqual(len(_violations("switch (state) {")), 0)

    def test_return_call(self) -> None:
        self.assertEqual(len(_violations("return getValue();")), 0)

    def test_typedef(self) -> None:
        self.assertEqual(len(_violations("typedef void (*cb_t)(int);")), 0)

    def test_using(self) -> None:
        self.assertEqual(len(_violations("using cb_t = void(*)(int);")), 0)

    def test_macro_call_allcaps(self) -> None:
        self.assertEqual(len(_violations('FL_WARN("msg");')), 0)

    def test_macro_fl_assert(self) -> None:
        self.assertEqual(len(_violations('FL_ASSERT(false, "bad");')), 0)

    def test_macro_esp_error(self) -> None:
        self.assertEqual(len(_violations("ESP_ERROR_CHECK(ret);")), 0)

    def test_operator_overload(self) -> None:
        self.assertEqual(len(_violations("bool operator==(const Foo& o) const;")), 0)

    def test_operator_call(self) -> None:
        self.assertEqual(len(_violations("void operator()(T* ptr) const {")), 0)

    def test_ternary_continuation(self) -> None:
        self.assertEqual(len(_violations(": foo(x, y);")), 0)

    def test_static_cast(self) -> None:
        self.assertEqual(len(_violations("auto x = static_cast<int>(y);")), 0)


# ── Realistic patterns ─────────────────────────────────────────────────────


class TestRealistic(unittest.TestCase):
    def test_channel_engine_class(self) -> None:
        code = """\
class ChannelEngineSpi {
    void show();
    void poll();
    bool canHandle(const ChannelDataPtr& data) const;
};"""
        self.assertEqual(len(_violations(code)), 3)

    def test_channel_engine_with_noexcept(self) -> None:
        code = """\
class ChannelEngineSpi {
    void show() FL_NOEXCEPT;
    void poll() FL_NOEXCEPT;
    bool canHandle(const ChannelDataPtr& data) const FL_NOEXCEPT;
};"""
        self.assertEqual(len(_violations(code)), 0)

    def test_mixed(self) -> None:
        code = """\
class Foo {
    Foo();
    ~Foo();
    Foo(const Foo&) = delete;
    void go() FL_NOEXCEPT;
    void stop();
};"""
        # Foo() constructor + stop() = 2 violations
        self.assertEqual(len(_violations(code)), 2)


if __name__ == "__main__":
    unittest.main()
