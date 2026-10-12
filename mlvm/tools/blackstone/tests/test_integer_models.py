"""Finite-width reference models for the 20 HIR bitvector identities.

These tests evaluate the actual declarative input/output expressions over defined
bitvectors. They are mathematical evidence, not a NodeIR executor or formal proof.
"""

import random
import unittest

from tools.blackstone.catalog import load


def evaluate(expression, environment, width):
    mask = (1 << width) - 1
    if isinstance(expression, dict):
        return environment[expression["Symbol"]]
    operator = expression[0]["Symbol"]
    args = [evaluate(arg, environment, width) for arg in expression[1:]]
    if operator == "and":
        return args[0] & args[1]
    if operator == "or":
        return args[0] | args[1]
    if operator == "xor":
        return args[0] ^ args[1]
    if operator == "not":
        return (~args[0]) & mask
    if operator == "shl":
        return (args[0] << args[1]) & mask
    if operator == "lshr":
        return args[0] >> args[1]
    if operator == "ashr":
        signed = args[0] - (1 << width) if args[0] & (1 << (width - 1)) else args[0]
        return (signed >> args[1]) & mask
    if operator == "udiv":
        return args[0] // args[1]
    if operator == "urem":
        return args[0] % args[1]
    raise ValueError(f"unsupported reference operator {operator}")


class IntegerModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.kernels = [k for k in load().kernels.values() if k["stage"] == "hir"]

    def check_pair(self, kernel, width, x, y, exponent):
        environment = {"x": x, "y": y, "mask": (1 << width) - 1,
                       "zero": 0, "one": 1, "exponent": exponent,
                       "power": 1 << exponent, "low-mask": (1 << exponent) - 1}
        _, original, replacement = kernel["transform"]
        self.assertEqual(evaluate(original, environment, width),
                         evaluate(replacement, environment, width),
                         (kernel["id"], width, x, y, exponent))

    def test_exhaustive_small_width_bitvectors(self):
        for kernel in self.kernels:
            with self.subTest(kernel=kernel["id"]):
                for width in range(1, 6):
                    for x in range(1 << width):
                        for y in range(1 << width):
                            for exponent in range(width):
                                self.check_pair(kernel, width, x, y, exponent)

    def test_realistic_width_boundaries_and_seeded_samples(self):
        rng = random.Random(0xB1AC57)
        for width in (8, 16, 32, 64, 128):
            mask = (1 << width) - 1
            values = [0, 1, mask, mask - 1, 1 << (width - 1), (1 << (width - 1)) - 1]
            values += [rng.getrandbits(width) for _ in range(12)]
            for kernel in self.kernels:
                for x in values:
                    for y in values:
                        for exponent in (0, width // 2, width - 1):
                            self.check_pair(kernel, width, x, y, exponent)

    def test_excluded_unsigned_zero_divisor(self):
        # Demonstrates the relevant semantic boundary rather than claiming
        # the identity for an invalid divisor.
        original = next(k for k in self.kernels if k["id"].endswith("divide-power-of-two"))["transform"][1]
        with self.assertRaises(ZeroDivisionError):
            evaluate(original, {"x": 255, "power": 0}, 8)

    def test_reference_evidence_is_limited_to_hir(self):
        self.assertEqual(len(self.kernels), 20)
        with self.assertRaises(ValueError):
            evaluate([{"Symbol": "load"}, {"Symbol": "x"}], {"x": 0}, 8)


if __name__ == "__main__":
    unittest.main()
