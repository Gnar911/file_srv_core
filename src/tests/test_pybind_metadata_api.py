from __future__ import annotations

import uuid

import pytest

import fs_core


def _make_record(ts: float, can_id: int, direction: int, channel: str, changed_payload_byte: int) -> fs_core.LogRecord:
    rec = fs_core.LogRecord()
    rec.timestamp = ts
    rec.can_id = can_id
    rec.direction = direction
    rec.data_len = 8
    rec.data = bytes([changed_payload_byte] * 8)
    rec.channel = channel
    return rec


def test_pybind_metadata_api_end_to_end() -> None:
    token = f"pybind_metadata_{uuid.uuid4().hex}"
    storage = fs_core.MetaDataStorageInterface(token)

    entries = [
        _make_record(1.0, 0x120, 0, " CAN0 ", 0x11),
        _make_record(2.5, 0x121, 1, "can1", 0x22),
        _make_record(4.0, 0x120, 1, "can0", 0x33),
        _make_record(5.5, 0x130, 0, "   ", 0x44),
    ]

    storage.write_entries(entries)

    db = fs_core.LogIndexDatabase(storage.token_path())

    timestamps = db.get_metadata(fs_core.MetadataType.BOUNDED_TIMESTAMP)
    can_ids = db.get_metadata(fs_core.MetadataType.CAN_IDS)
    channels = db.get_metadata(fs_core.MetadataType.CHANNELS)
    total = db.get_metadata(fs_core.MetadataType.TOTAL)

    assert timestamps.first == pytest.approx(1.0)
    assert timestamps.last == pytest.approx(5.5)
    assert can_ids == [0x120, 0x121, 0x130]
    assert channels == ["can0", "can1", "unknown"]
    assert total == 4
