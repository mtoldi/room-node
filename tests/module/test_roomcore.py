"""Module tests for roomcore. IDs match docs/test-plan.md."""

import math

import pytest

from conftest import mv_for

AUTO, OFF, ON = 0, 1, 2
RC_OK, RC_ERR_RANGE = 0, 1

TMP117_FAULT = 1 << 0
SHTC3_FAULT = 1 << 1
SIMULATION = 1 << 3
FROST = 1 << 4
MAX_RUNTIME = 1 << 5
OVERRIDE = 1 << 6

HOUR = 3600


def test_mt01_hysteresis(make):
    rc = make(min_switch_ms=0).set(t=21.0, setpoint=21.0).run(1)
    assert not rc.heating

    rc.set(t=20.75).run(1)
    assert not rc.heating, "inside the band: no change"

    rc.set(t=20.65).run(1)
    assert rc.heating and rc.priority == 16

    rc.set(t=21.25).run(1)
    assert rc.heating, "inside the band: no change"

    rc.set(t=21.35).run(1)
    assert not rc.heating


def test_mt02_priority_array(make):
    rc = make(min_switch_ms=0).set(t=18.0, setpoint=21.0).run(1)
    assert rc.heating and rc.priority == 16

    assert rc.set_mode(OFF) == RC_OK
    rc.run(1)
    assert not rc.heating and rc.priority == 8

    rc.set_mode(ON)
    rc.set(t=25.0).run(1)
    assert rc.heating and rc.priority == 8, "operator beats the thermostat"

    rc.set_mode(AUTO)
    rc.run(1)
    assert not rc.heating and rc.priority == 16, "relinquishing 8 hands control back"

    assert rc.set_mode(7) == RC_ERR_RANGE


def test_mt03_minimum_switch_time(make):
    rc = make().set(t=18.0, setpoint=21.0).run(1)
    assert not rc.heating and rc.priority == 6, "output held off after boot"

    rc.run(298)
    assert not rc.heating
    rc.run(2)
    assert rc.heating

    rc.set(t=24.0).run(298)
    assert rc.heating and rc.priority == 6, "minimum on time"
    rc.run(3)
    assert not rc.heating

    rc.set(t=18.0)
    rc.set_mode(ON)
    rc.run(10)
    assert not rc.heating, "the operator cannot break the minimum off time either"
    rc.run(300)
    assert rc.heating and rc.priority == 6, "the new state is latched again"
    rc.run(301)
    assert rc.heating and rc.priority == 8


def test_mt04_frost_protection(make):
    rc = make().set(t=5.0).run(1)
    assert rc.heating and rc.priority == 5, "frost overrides the boot hold"
    assert rc.status & FROST

    rc.set_mode(OFF)
    rc.run(1)
    assert rc.heating, "frost overrides mode off"

    rc.set(t=7.5).run(10)
    assert rc.heating and rc.status & FROST, "released only above 8 °C"

    rc.set(t=8.5).run(1)
    assert not rc.status & FROST
    assert rc.heating and rc.priority == 6, "minimum on time after frost ends"

    rc.run(300)
    assert not rc.heating and rc.priority == 6, "off is latched for the minimum time"
    rc.run(301)
    assert not rc.heating and rc.priority == 8


def test_mt05_failsafe(make):
    rc = make().set(t=5.0).run(1)
    assert rc.heating

    rc.set(tmp_ok=False, sht_ok=False).run(0.2)
    assert not rc.heating, "no minimum on time when both sensors are lost"
    assert rc.priority == 1
    assert not rc.t_ref_valid
    assert rc.status & TMP117_FAULT and rc.status & SHTC3_FAULT


def test_mt06_fallback_to_shtc3(make):
    rc = make(min_switch_ms=0).set(t=21.0, t_aux=18.0, setpoint=21.0).run(1)
    assert rc.t_ref == pytest.approx(21.0), "TMP117 is the reference"

    rc.set(tmp_ok=False).run(1)
    assert rc.t_ref == pytest.approx(18.0)
    assert rc.status & TMP117_FAULT and not rc.status & SHTC3_FAULT
    assert rc.heating


def test_mt07_setpoint(make, lib):
    rc = make().set(setpoint=21.0).run(2)
    assert rc.setpoint == 21.0

    for bad in (26.5, 15.9, math.nan):
        assert rc.write_setpoint(bad) == RC_ERR_RANGE
    rc.run(1)
    assert rc.setpoint == 21.0 and not rc.status & OVERRIDE

    assert rc.write_setpoint(23.5) == RC_OK
    rc.run(1)
    assert rc.setpoint == 23.5 and rc.status & OVERRIDE

    rc.set(setpoint=21.2).run(5)
    assert rc.setpoint == 23.5, "small slider movement keeps the override"

    rc.set(setpoint=21.6).run(5)
    assert rc.setpoint == 21.6 and not rc.status & OVERRIDE, "moving the slider ends it"

    rc.write_setpoint(24.0)
    rc.relinquish_setpoint()
    rc.run(1)
    assert rc.setpoint == 21.6

    assert lib.rc_pot_to_celsius(0) == 16.0
    assert lib.rc_pot_to_celsius(100) == 16.0
    assert lib.rc_pot_to_celsius(1550) == pytest.approx(21.0)
    assert lib.rc_pot_to_celsius(3000) == 26.0
    assert lib.rc_pot_to_celsius(3300) == 26.0


def test_mt14_setpoint_stable_under_adc_noise(make):
    """A resting slider right at a rounding boundary (21.35 °C) must not make
    the setpoint flicker between 21.3 and 21.4."""
    rc = make().set(pot_mv=mv_for(21.35)).run(2)
    seen = set()
    for i in range(200):
        rc.set(pot_mv=mv_for(21.35) + (6 if i % 2 else -6)).run(0.2)
        seen.add(rc.setpoint)
    assert len(seen) == 1, f"setpoint flickered: {sorted(seen)}"

    rc.set(setpoint=21.8).run(5)
    assert rc.setpoint == 21.8, "real slider movement still comes through"


def test_mt08_max_runtime_alarm(make):
    rc = make(min_switch_ms=0).set(t=18.0).run(1)
    assert rc.heating

    rc.run(2 * HOUR - 10, tick_ms=1000)
    assert not rc.status & MAX_RUNTIME

    rc.run(20, tick_ms=1000)
    assert rc.status & MAX_RUNTIME
    assert rc.heating, "the alarm does not switch the heating off"


def test_mt12_simulated_day(make):
    """24 h in the demo configuration: the loop settles around the setpoint
    without short cycling."""
    rc = make(min_switch_ms=10_000, simulation=True).set(t=20.0, setpoint=22.0)
    rc.run(HOUR, tick_ms=1000)
    assert rc.status & SIMULATION

    temps, switch_ons, on_since, shortest_on = [], 0, None, math.inf
    for second in range(23 * HOUR):
        was_on = rc.heating
        rc.run(1, tick_ms=1000)
        temps.append(rc.t_ref)
        if rc.heating and not was_on:
            switch_ons += 1
            on_since = second
        if was_on and not rc.heating and on_since is not None:
            shortest_on = min(shortest_on, second - on_since)

    assert 21.4 <= min(temps) and max(temps) <= 22.6
    assert 5 <= switch_ons / 23 <= 40, "oscillates, but does not chatter"
    assert shortest_on >= 10


def test_mt13_clock_wraparound(make):
    """millis() wraps after 49.7 days; timing must keep working across it."""
    rc = make(start_ms=0xFFFFFFFF - 60_000).set(t=18.0, setpoint=21.0)
    rc.run(299)
    assert not rc.heating
    rc.run(2)
    assert rc.heating
