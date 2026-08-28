from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FieldMobyListsContractTest(unittest.TestCase):
    def test_retained_builder_is_state_only(self) -> None:
        generated = "\n".join(
            path.read_text() for path in sorted((ROOT / "generated").glob("shard_*.c"))
        )
        match = re.search(
            r"void gen_func_800521C0\(Core\* c\) \{(?P<body>.*?)\n\}",
            generated,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertNotIn("func_", body)
        self.assertNotIn("gpu", body.lower())
        self.assertNotIn("0x8005DBC4", body)
        self.assertNotIn("0x8005DD0C", body)
        self.assertIn("c->mem_w32", body)
        self.assertIn("c->mem_w8", body)


if __name__ == "__main__":
    unittest.main()
