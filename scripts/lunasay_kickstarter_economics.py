#!/usr/bin/env python3
"""Audit LunaSay Kickstarter rewards against a quote-backed cost model.

The calculator refuses to declare the model launch-ready while any required
cost is missing. Use --allow-incomplete to print the known-cost lower bound
while collecting quotes; that output must never be treated as a funding goal.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_MODEL = ROOT / "config" / "lunasay_kickstarter_economics.json"


def money(value: float, currency: str) -> str:
    symbol = "$" if currency == "USD" else f"{currency} "
    return f"{symbol}{value:,.2f}"


def require_rate(name: str, value: Any) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError(f"{name} must be numeric")
    number = float(value)
    if not 0 <= number < 1:
        raise ValueError(f"{name} must be at least 0 and less than 1")
    return number


def optional_amount(name: str, value: Any) -> float | None:
    if value is None:
        return None
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        raise ValueError(f"{name} must be numeric or null")
    number = float(value)
    if number < 0:
        raise ValueError(f"{name} cannot be negative")
    return number


def load_model(path: Path) -> dict[str, Any]:
    try:
        model = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise ValueError(f"model does not exist: {path}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON in {path}: {exc}") from exc
    if not isinstance(model, dict):
        raise ValueError("model root must be an object")
    return model


def calculate(model: dict[str, Any]) -> dict[str, Any]:
    currency = str(model.get("currency", "USD"))
    rewards = model.get("hardware_rewards")
    if not isinstance(rewards, list) or not rewards:
        raise ValueError("hardware_rewards must be a non-empty array")

    reward_rows: list[dict[str, Any]] = []
    total_units = 0
    reward_revenue = 0.0
    for index, reward in enumerate(rewards):
        if not isinstance(reward, dict):
            raise ValueError(f"hardware_rewards[{index}] must be an object")
        name = str(reward.get("name", f"reward {index + 1}"))
        quantity = reward.get("quantity")
        price = reward.get("price")
        if not isinstance(quantity, int) or isinstance(quantity, bool) or quantity <= 0:
            raise ValueError(f"{name}.quantity must be a positive integer")
        if not isinstance(price, (int, float)) or isinstance(price, bool) or price <= 0:
            raise ValueError(f"{name}.price must be positive")
        row_revenue = quantity * float(price)
        reward_rows.append(
            {"name": name, "quantity": quantity, "price": float(price), "revenue": row_revenue}
        )
        total_units += quantity
        reward_revenue += row_revenue

    spare_rate = require_rate("service_spare_rate", model.get("service_spare_rate"))
    spare_units = math.ceil(total_units * spare_rate)
    contingency_rate = require_rate(
        "cost_contingency_rate", model.get("cost_contingency_rate")
    )

    fee_rates = model.get("fee_rates")
    if not isinstance(fee_rates, dict) or not fee_rates:
        raise ValueError("fee_rates must be a non-empty object")
    normalized_fees = {
        name: require_rate(f"fee_rates.{name}", value)
        for name, value in fee_rates.items()
    }
    total_fee_rate = sum(normalized_fees.values())
    if total_fee_rate >= 1:
        raise ValueError("combined fee and reserve rate must be less than 1")

    per_reward = model.get("per_reward_costs")
    fixed = model.get("fixed_costs")
    if not isinstance(per_reward, dict) or not per_reward:
        raise ValueError("per_reward_costs must be a non-empty object")
    if not isinstance(fixed, dict) or not fixed:
        raise ValueError("fixed_costs must be a non-empty object")

    missing: list[dict[str, str]] = []
    variable_known = 0.0
    variable_complete = 0.0
    cost_rows: list[dict[str, Any]] = []
    for name, entry in per_reward.items():
        if not isinstance(entry, dict):
            raise ValueError(f"per_reward_costs.{name} must be an object")
        amount = optional_amount(f"per_reward_costs.{name}.amount", entry.get("amount"))
        source = str(entry.get("source", ""))
        spare_applies = bool(entry.get("applies_to_service_spares", False))
        multiplier = total_units + (spare_units if spare_applies else 0)
        extended = None if amount is None else amount * multiplier
        cost_rows.append(
            {
                "name": name,
                "amount": amount,
                "source": source,
                "units": multiplier,
                "extended": extended,
            }
        )
        if amount is None:
            missing.append({"field": f"per_reward_costs.{name}", "source": source})
        else:
            variable_known += extended or 0.0
            variable_complete += extended or 0.0

    fixed_known = 0.0
    fixed_rows: list[dict[str, Any]] = []
    for name, entry in fixed.items():
        if not isinstance(entry, dict):
            raise ValueError(f"fixed_costs.{name} must be an object")
        amount = optional_amount(f"fixed_costs.{name}.amount", entry.get("amount"))
        source = str(entry.get("source", ""))
        fixed_rows.append({"name": name, "amount": amount, "source": source})
        if amount is None:
            missing.append({"field": f"fixed_costs.{name}", "source": source})
        else:
            fixed_known += amount

    known_cost = variable_known + fixed_known
    known_with_contingency = known_cost * (1 + contingency_rate)
    gross_known_lower_bound = known_with_contingency / (1 - total_fee_rate)
    deductions_at_capacity = reward_revenue * total_fee_rate
    net_capacity = reward_revenue - deductions_at_capacity
    remaining_after_known = net_capacity - known_with_contingency
    complete = not missing

    report: dict[str, Any] = {
        "launch_ready": complete,
        "currency": currency,
        "reward_rows": reward_rows,
        "hardware_reward_units": total_units,
        "service_spare_units": spare_units,
        "weighted_average_hardware_pledge": reward_revenue / total_units,
        "hardware_reward_revenue_capacity": reward_revenue,
        "fee_rates": normalized_fees,
        "combined_fee_and_reserve_rate": total_fee_rate,
        "deductions_at_reward_capacity": deductions_at_capacity,
        "net_reward_capacity_after_fees_and_reserve": net_capacity,
        "cost_contingency_rate": contingency_rate,
        "per_reward_cost_rows": cost_rows,
        "fixed_cost_rows": fixed_rows,
        "known_cost_before_contingency": known_cost,
        "known_cost_with_contingency": known_with_contingency,
        "known_cost_gross_lower_bound": gross_known_lower_bound,
        "remaining_net_capacity_after_known_costs": remaining_after_known,
        "missing_required_costs": missing,
        "shipping_and_tax_collected_separately": bool(
            model.get("shipping_and_tax_collected_separately", False)
        ),
    }

    if complete:
        total_cost = variable_complete + fixed_known
        total_with_contingency = total_cost * (1 + contingency_rate)
        required_gross_goal = total_with_contingency / (1 - total_fee_rate)
        report.update(
            {
                "total_cost_before_contingency": total_cost,
                "total_cost_with_contingency": total_with_contingency,
                "minimum_safe_gross_goal": required_gross_goal,
                "reward_capacity_surplus_or_shortfall": reward_revenue - required_gross_goal,
                "minimum_hardware_backers_at_weighted_average": math.ceil(
                    required_gross_goal / (reward_revenue / total_units)
                ),
            }
        )
    return report


def text_report(report: dict[str, Any]) -> str:
    currency = report["currency"]
    lines = [
        "LunaSay Kickstarter economics audit",
        f"Status: {'LAUNCH-READY MODEL' if report['launch_ready'] else 'INCOMPLETE — NOT A FUNDING GOAL'}",
        f"Hardware rewards: {report['hardware_reward_units']}",
        f"Service spares: {report['service_spare_units']}",
        f"Weighted hardware pledge: {money(report['weighted_average_hardware_pledge'], currency)}",
        f"Hardware reward revenue capacity: {money(report['hardware_reward_revenue_capacity'], currency)}",
        f"Combined fees/reserve: {report['combined_fee_and_reserve_rate'] * 100:.1f}%",
        f"Net reward capacity after fees/reserve: {money(report['net_reward_capacity_after_fees_and_reserve'], currency)}",
        f"Known costs with contingency: {money(report['known_cost_with_contingency'], currency)}",
        f"Remaining net capacity after known costs: {money(report['remaining_net_capacity_after_known_costs'], currency)}",
    ]
    if report["launch_ready"]:
        lines += [
            f"Minimum safe gross goal: {money(report['minimum_safe_gross_goal'], currency)}",
            "Reward-capacity surplus/shortfall: "
            + money(report["reward_capacity_surplus_or_shortfall"], currency),
            "Minimum hardware backers at weighted pledge: "
            + str(report["minimum_hardware_backers_at_weighted_average"]),
        ]
    else:
        lines += [
            f"Known-cost gross lower bound only: {money(report['known_cost_gross_lower_bound'], currency)}",
            "Missing required costs:",
        ]
        for item in report["missing_required_costs"]:
            lines.append(f"- {item['field']}: {item['source']}")
        lines.append("Do not use the lower bound as the Kickstarter funding goal.")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    parser.add_argument("--json", action="store_true", help="emit machine-readable JSON")
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help="exit zero while required quote fields remain missing",
    )
    args = parser.parse_args()
    try:
        report = calculate(load_model(args.model))
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if args.json:
        print(json.dumps(report, indent=2))
    else:
        print(text_report(report), end="")
    if not report["launch_ready"] and not args.allow_incomplete:
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
