"""バランスレポート向けの、外部依存を持たない小規模な統計補助処理。"""

from __future__ import annotations

import math
import statistics
from typing import Any, Iterable


def confidence_z(confidence_level: float) -> float:
    """信頼水準に対応する両側正規分布の臨界値を返す。"""

    level = min(0.999, max(0.50, float(confidence_level)))
    return statistics.NormalDist().inv_cdf(0.5 + level / 2.0)


def wilson_interval(
    successes: int,
    sample_count: int,
    confidence_level: float = 0.95,
) -> tuple[float, float]:
    """二項比率に対するWilsonスコア区間を求める。"""

    if sample_count <= 0:
        return 0.0, 1.0
    successes = min(sample_count, max(0, int(successes)))
    z = confidence_z(confidence_level)
    proportion = successes / sample_count
    denominator = 1.0 + z * z / sample_count
    centre = (
        proportion + z * z / (2.0 * sample_count)
    ) / denominator
    margin = (
        z
        * math.sqrt(
            proportion * (1.0 - proportion) / sample_count
            + z * z / (4.0 * sample_count * sample_count)
        )
        / denominator
    )
    return max(0.0, centre - margin), min(1.0, centre + margin)


def mean_interval(
    samples: Iterable[float],
    confidence_level: float = 0.95,
    lower_bound: float | None = None,
    upper_bound: float | None = None,
) -> tuple[float, float]:
    """標本平均に対する正規近似区間を求める。"""

    values = [float(value) for value in samples]
    if not values:
        lower = lower_bound if lower_bound is not None else 0.0
        upper = upper_bound if upper_bound is not None else lower
        return lower, upper
    centre = statistics.fmean(values)
    if len(values) < 2:
        lower = centre
        upper = centre
    else:
        margin = (
            confidence_z(confidence_level)
            * statistics.stdev(values)
            / math.sqrt(len(values))
        )
        lower = centre - margin
        upper = centre + margin
    if lower_bound is not None:
        lower = max(lower_bound, lower)
    if upper_bound is not None:
        upper = min(upper_bound, upper)
    return lower, upper


def median_interval(
    samples: Iterable[float],
    confidence_level: float = 0.95,
) -> tuple[float, float]:
    """中央値に対する、分布を仮定しない順序統計量近似を求める。"""

    values = sorted(float(value) for value in samples)
    count = len(values)
    if not values:
        return 0.0, 0.0
    if count == 1:
        return values[0], values[0]
    z = confidence_z(confidence_level)
    lower_index = max(
        0,
        int(math.floor((count - z * math.sqrt(count)) / 2.0)),
    )
    upper_index = min(count - 1, count - lower_index - 1)
    return values[lower_index], values[upper_index]


def classify_interval(
    interval: tuple[float, float],
    target_minimum: float,
    target_maximum: float,
) -> str:
    """閉区間の目標範囲に対して、区間を分類する。"""

    lower, upper = interval
    if upper < target_minimum:
        return "below_target"
    if lower > target_maximum:
        return "above_target"
    if lower >= target_minimum and upper <= target_maximum:
        return "within_target"
    return "inconclusive"


def rounded_interval(
    interval: tuple[float, float],
    digits: int = 4,
) -> dict[str, Any]:
    return {
        "lower": round(interval[0], digits),
        "upper": round(interval[1], digits),
    }
