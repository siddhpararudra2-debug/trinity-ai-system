"""Phase 4: rule-list router — verbs, units, dims, synonyms, math, failure.

The hard contract is the golden sentence: "Create a 50 mm quadcopter
frame" must parse to exactly the dict the old single-regex router
produced (including the float overall_size). Everything else is new
ability, guarded the same way: parse real sentences, push them through
run_chain and POST /requirements/execute, and check the enriched
failure details (nearest parts + example sentences).
"""

import pytest
from fastapi.testclient import TestClient

from src.core.errors import RequestValidationError
from src.intelligence.router import EXAMPLE_SENTENCES, RULES, parse_requirement
from src.main import app
from src.prompt_engineering.chain import run_chain

GOLDEN_SENTENCE = "Create a 50 mm quadcopter frame"
GOLDEN_PARSE = {
    "domain": "cad",
    "operation": "generate",
    "object": "quadcopter_frame",
    "parameters": {"overall_size": 50.0},
    "units": "mm",
}


# ------------------------------------------------------------- golden ---


def test_original_sentence_parses_to_the_same_output():
    parsed = parse_requirement(GOLDEN_SENTENCE)
    assert parsed == GOLDEN_PARSE
    assert isinstance(parsed["parameters"]["overall_size"], float)


def test_rule_list_is_named_and_ordered():
    assert [rule.name for rule in RULES] == [
        "math_solve",
        "quadcopter_frame",
        "enclosure",
        "mounting_plate",
        "l_bracket",
        "standoff",
    ]
    for rule in RULES:
        assert rule.phrases
        assert callable(rule.extractor)


# -------------------------------------------------------------- verbs ---


@pytest.mark.parametrize(
    "verb",
    ["make", "build", "design", "create", "generate", "made", "created", "building"],
)
def test_verb_synonyms_all_parse(verb):
    parsed = parse_requirement(f"{verb} a 50 mm quadcopter frame")
    assert parsed["parameters"] == {"overall_size": 50.0}


def test_verb_is_required():
    with pytest.raises(RequestValidationError):
        parse_requirement("quadcopter frame 50 mm")


def test_part_phrase_is_required():
    with pytest.raises(RequestValidationError):
        parse_requirement("create a 50 mm flux capacitor")


def test_parsing_is_case_insensitive():
    parsed = parse_requirement("CREATE A 50 MM QUADCOPTER FRAME")
    assert parsed == GOLDEN_PARSE


# --------------------------------------------------------------- units ---


@pytest.mark.parametrize(
    ("sentence", "expected"),
    [
        ("Create a 5 cm quadcopter frame", 50.0),
        ("Create a 1.5 m quadcopter frame", 1500.0),
        ("Create a 20 inch quadcopter frame", pytest.approx(508.0)),
        ("Create a 20 inches quadcopter frame", pytest.approx(508.0)),
        ("Create a 50 quadcopter frame", 50.0),
    ],
)
def test_unit_conversion_normalises_to_mm(sentence, expected):
    parsed = parse_requirement(sentence)
    assert parsed["parameters"]["overall_size"] == expected
    assert parsed["units"] == "mm"


def test_each_number_uses_its_own_unit():
    parsed = parse_requirement("make a 1 m x 50 cm x 30 cm box")
    assert parsed["parameters"] == {"length": 1000.0, "width": 500.0, "height": 300.0}


def test_trailing_unit_falls_back_for_bare_numbers():
    parsed = parse_requirement("make a box 4 x 3 cm")
    assert parsed["parameters"] == {"length": 40.0, "width": 30.0}


# ----------------------------------------------------- dimension patterns ---


def test_dimension_pattern_enclosure():
    parsed = parse_requirement("Make a box 100 x 60 x 40 mm")
    assert parsed == {
        "domain": "cad",
        "operation": "generate",
        "object": "enclosure",
        "parameters": {"length": 100.0, "width": 60.0, "height": 40.0},
        "units": "mm",
    }


@pytest.mark.parametrize("separator", ["x", "×", "by"])
def test_dimension_separators(separator):
    parsed = parse_requirement(
        f"design an enclosure 100 {separator} 60 {separator} 40 mm"
    )
    assert parsed["parameters"] == {
        "length": 100.0,
        "width": 60.0,
        "height": 40.0,
    }


@pytest.mark.parametrize(
    ("sentence", "object_name", "parameters"),
    [
        ("Create a 250 mm drone frame", "quadcopter_frame", {"overall_size": 250.0}),
        ("Make a 20 mm standoff", "standoff", {"height": 20.0}),
        (
            "Create a mounting plate 100 x 60 mm",
            "mounting_plate",
            {"length": 100.0, "width": 60.0},
        ),
        (
            "Build a bracket 40 x 40 x 20 mm",
            "l_bracket",
            {"base_length": 40.0, "height": 40.0, "width": 20.0},
        ),
    ],
)
def test_dimension_patterns_per_part(sentence, object_name, parameters):
    parsed = parse_requirement(sentence)
    assert parsed["object"] == object_name
    assert parsed["parameters"] == parameters


# ---------------------------------------------------------- named values ---


def test_named_thickness():
    parsed = parse_requirement("Design an enclosure 3 mm thick")
    assert parsed["parameters"] == {"wall_thickness": 3.0}


def test_named_motor_count_is_an_integer():
    parsed = parse_requirement("Build a drone frame with 4 motors")
    assert parsed == {
        "domain": "cad",
        "operation": "generate",
        "object": "quadcopter_frame",
        "parameters": {"motor_count": 4},
        "units": "mm",
    }
    assert isinstance(parsed["parameters"]["motor_count"], int)


def test_non_integer_motor_count_is_rejected():
    with pytest.raises(RequestValidationError, match="must be an integer"):
        parse_requirement("build a drone frame with 4.5 motors")


def test_named_values_coexist_with_dimensions():
    parsed = parse_requirement("make a box 100 x 60 x 40 mm with 3 mm thick walls")
    assert parsed["parameters"] == {
        "length": 100.0,
        "width": 60.0,
        "height": 40.0,
        "wall_thickness": 3.0,
    }


# -------------------------------------------------------- part synonyms ---


@pytest.mark.parametrize(
    ("sentence", "object_name"),
    [
        ("Create a drone frame 50 mm", "quadcopter_frame"),
        ("Create a quadcopter frame 50 mm", "quadcopter_frame"),
        ("Make a box 100 mm", "enclosure"),
        ("Make an enclosure 100 mm", "enclosure"),
        ("Design a bracket 40 mm", "l_bracket"),
        ("Design an l-bracket 40 mm", "l_bracket"),
        ("Create a mounting plate 100 mm", "mounting_plate"),
        ("Make a standoff 20 mm", "standoff"),
    ],
)
def test_part_name_synonyms(sentence, object_name):
    assert parse_requirement(sentence)["object"] == object_name


def test_mounting_plate_beats_generic_bracket_when_both_appear():
    parsed = parse_requirement("create a mounting plate bracket 100 x 60 mm")
    assert parsed["object"] == "mounting_plate"


# --------------------------------------------------------------- math ---


def test_math_rule_parses_solve():
    parsed = parse_requirement("Solve x**2 - 4 = 0")
    assert parsed == {
        "domain": "math",
        "operation": "solve",
        "object": "expression",
        "parameters": {"expression": "x**2 - 4 = 0"},
        "units": "",
    }


def test_run_chain_builds_math_toolcall():
    out = run_chain("Solve x**2 - 4 = 0")
    assert out["plan"] == ["n1"]
    assert out["toolcall"] == {
        "engine": "math",
        "operation": "solve",
        "parameters": {"expression": "x**2 - 4 = 0"},
    }


def test_requirements_execute_routes_math_end_to_end():
    with TestClient(app) as client:
        resp = client.post(
            "/api/requirements/execute", json={"text": "Solve x**2 - 4 = 0"}
        )
        body = resp.json()
    assert resp.status_code == 200
    assert body["success"] is True, body.get("errors")
    assert body["engine"] == "math"
    assert body["operation"] == "solve"
    assert sorted(body["result"]["result"]) == [-2.0, 2.0]


# ------------------------------------------------------------- failure ---


def test_failure_returns_nearest_parts_and_example_sentences():
    with pytest.raises(RequestValidationError) as excinfo:
        parse_requirement("do something unparseable xyz")
    details = excinfo.value.details
    assert details["parser_scope"]
    assert details["supported_example"] == GOLDEN_SENTENCE
    assert isinstance(details["nearest_parts"], list)
    assert details["examples"]
    assert "Solve x**2 - 4 = 0" in details["examples"]


def test_failure_nearest_parts_include_the_close_part_name():
    with pytest.raises(RequestValidationError) as excinfo:
        parse_requirement("make a quadcopter freem")
    assert "quadcopter_frame" in excinfo.value.details["nearest_parts"]


def test_every_example_sentence_parses():
    for part, sentences in EXAMPLE_SENTENCES.items():
        for sentence in sentences:
            parsed = parse_requirement(sentence)
            if part == "math":
                assert parsed["domain"] == "math"
            else:
                assert parsed["object"] == part


def test_non_positive_size_error_matches_original_contract():
    with pytest.raises(RequestValidationError) as excinfo:
        parse_requirement("Create a 0 mm quadcopter frame")
    assert excinfo.value.message == (
        "Quadcopter frame size must be a positive number of millimetres"
    )
    assert excinfo.value.details == {
        "overall_size": 0.0,
        "supported_example": GOLDEN_SENTENCE,
    }
