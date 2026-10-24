import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # run: python test_voice_timing.py
from voice import word_timings  # noqa: E402

text = "Sure thing! I added buy milk for tomorrow, 10 minutes."
spoken = [(0.05, "Sure"), (0.3, "thing"), (0.9, "I"), (1.0, "added"), (1.4, "buy"), (1.6, "milk"), (1.9, "for"),
          (2.1, "tomorrow"), (2.8, "ten"), (3.1, "minutes")]
t = word_timings(text, spoken, 3.8)
print(t)
assert t[0] == [50, 1] and t[-1][1] == len(text.split())
assert [x[1] for x in t] == sorted(x[1] for x in t)
g = word_timings("Hello there friend", [], 2.0)
print(g)
assert g[0] == [0, 1] and g[-1][1] == 3
assert word_timings("", [], 1.0) == []
print("ALL OK")
