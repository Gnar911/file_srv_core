from dataclasses import dataclass, field
from typing import Optional, List, Dict, Callable, Any, Tuple, Set
from collections import defaultdict
from pathlib import Path
import mmap as _mmap
import struct
import heapq
from pydantic import BaseModel, Field, ConfigDict
from lw.logger_setup import LOG
from file_service.data.can_parser_api import DataMmapManagerClient, MmapHeaderConstract, ParsedEntry, ParsedEntryLayout, IndexMmapLayout


class RecordManifest(BaseModel):
    model_config = ConfigDict(extra="ignore")

    version: int = 1
    token_id: str
    mmap_capacity: int = 0

    data_segments: list[str] = Field(default_factory=list)
    index_segments: list[str] = Field(default_factory=list)
    channel_index_segments: list[str] = Field(default_factory=list)
    direction_index_segments: list[str] = Field(default_factory=list)

    @classmethod
    def from_dict(cls, payload: dict[str, Any]) -> "RecordManifest":
        return cls(
            version=int(payload.get("version", 1)),
            token_id=str(payload.get("token_id", "")),
            mmap_capacity=int(payload.get("mmap_capacity", 0)),
            data_segments=[str(item) for item in payload.get("data_segments", [])],
            index_segments=[str(item) for item in payload.get("index_segments", [])],
            channel_index_segments=[str(item) for item in payload.get("channel_index_segments", [])],
            direction_index_segments=[str(item) for item in payload.get("direction_index_segments", [])],
        )

    @classmethod
    def load(cls, manifest_path: str | Path) -> "RecordManifest | None":
        path = Path(manifest_path)
        if not path.exists():
            return None
        try:
            return cls.model_validate_json(path.read_text(encoding="utf-8"))
        except Exception:
            return None

# Backwards-friendly alias used by the compact handler API
MmapFamilyManifest = RecordManifest

# .data.mmap
# .index.mmap
@dataclass
class CANLogRawDiskFile:
    manifest: RecordManifest
    _manifest_path: Optional[Path] = field(default=None, init=False, repr=False)

    _ENTRY_SIZE: int = field(default=ParsedEntryLayout.ENTRY_SIZE, init=False, repr=False)
    _DATA_HEADER_SIZE: int = field(default=MmapHeaderConstract.SIZE, init=False, repr=False)
    _ENTRY_LINE_NUMBER_OFFSET: int = field(default=ParsedEntryLayout.LINE_NUMBER_OFFSET, init=False, repr=False)
    _ENTRY_TIMESTAMP_OFFSET: int = field(default=ParsedEntryLayout.TIMESTAMP_OFFSET, init=False, repr=False)
    _ENTRY_LAST_TIMESTAMP_OFFSET: int = field(default=ParsedEntryLayout.LAST_TIMESTAMP_OFFSET, init=False, repr=False)
    _ENTRY_CAN_ID_OFFSET: int = field(default=ParsedEntryLayout.CAN_ID_OFFSET, init=False, repr=False)
    _ENTRY_DIRECTION_OFFSET: int = field(default=ParsedEntryLayout.DIRECTION_OFFSET, init=False, repr=False)
    _ENTRY_DATA_LEN_OFFSET: int = field(default=ParsedEntryLayout.DATA_LEN_OFFSET, init=False, repr=False)
    _ENTRY_CHANGED_OFFSET: int = field(default=ParsedEntryLayout.CHANGED_OFFSET, init=False, repr=False)
    _ENTRY_DATA_OFFSET: int = field(default=ParsedEntryLayout.DATA_OFFSET, init=False, repr=False)
    _ENTRY_DATA_CAPACITY: int = field(default=ParsedEntryLayout.DATA_CAPACITY, init=False, repr=False)
    _ENTRY_CHANNEL_OFFSET: int = field(default=ParsedEntryLayout.CHANNEL_OFFSET, init=False, repr=False)
    _ENTRY_CHANNEL_CAPACITY: int = field(default=ParsedEntryLayout.CHANNEL_CAPACITY, init=False, repr=False)
    _INDEX_HEADER_SIZE: int = field(default=IndexMmapLayout.INDEX_HEADER_SIZE, init=False, repr=False)
    _INDEX_CAN_ID_COUNT_OFFSET: int = field(default=IndexMmapLayout.INDEX_HEADER_CAN_ID_COUNT_OFFSET, init=False, repr=False)
    _INDEX_MAX_CAN_IDS_OFFSET: int = field(default=IndexMmapLayout.INDEX_HEADER_MAX_CAN_IDS_OFFSET, init=False, repr=False)
    _INDEX_MAX_ROW_POOL_SIZE_OFFSET: int = field(default=IndexMmapLayout.INDEX_HEADER_MAX_ROW_POOL_SIZE_OFFSET, init=False, repr=False)
    _INDEX_MAX_CHANGED_ROW_POOL_SIZE_OFFSET: int = field(default=IndexMmapLayout.INDEX_HEADER_MAX_CHANGED_ROW_POOL_SIZE_OFFSET, init=False, repr=False)
    _INDEX_MAX_TS_POOL_SIZE_OFFSET: int = field(default=IndexMmapLayout.INDEX_HEADER_MAX_TS_POOL_SIZE_OFFSET, init=False, repr=False)

    _INDEX_FILTER_SIZE: int = field(default=IndexMmapLayout.CAN_ID_FILTER_SIZE, init=False, repr=False)
    _INDEX_FILTER_CAN_ID_OFFSET: int = field(default=IndexMmapLayout.CAN_ID_FILTER_CAN_ID_OFFSET, init=False, repr=False)
    _INDEX_FILTER_ROW_OFFSET_OFFSET: int = field(default=IndexMmapLayout.CAN_ID_FILTER_ROW_OFFSET_OFFSET, init=False, repr=False)
    _INDEX_FILTER_CHANGED_ROW_OFFSET_OFFSET: int = field(default=IndexMmapLayout.CAN_ID_FILTER_CHANGED_ROW_OFFSET_OFFSET, init=False, repr=False)
    _INDEX_FILTER_TS_OFFSET_OFFSET: int = field(default=IndexMmapLayout.CAN_ID_FILTER_TS_OFFSET_OFFSET, init=False, repr=False)
    _INDEX_FILTER_COUNT_OFFSET: int = field(default=IndexMmapLayout.CAN_ID_FILTER_COUNT_OFFSET, init=False, repr=False)
    _INDEX_FILTER_CHANGED_COUNT_OFFSET: int = field(default=IndexMmapLayout.CAN_ID_FILTER_CHANGED_COUNT_OFFSET, init=False, repr=False)

    _CHANNEL_INDEX_HEADER_SIZE: int = field(default=IndexMmapLayout.CHANNEL_INDEX_HEADER_SIZE, init=False, repr=False)
    _CHANNEL_INDEX_CHANNEL_COUNT_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_INDEX_HEADER_CHANNEL_COUNT_OFFSET, init=False, repr=False)
    _CHANNEL_INDEX_MAX_CHANNELS_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_INDEX_HEADER_MAX_CHANNELS_OFFSET, init=False, repr=False)
    _CHANNEL_INDEX_MAX_ROW_POOL_SIZE_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_INDEX_HEADER_MAX_ROW_POOL_SIZE_OFFSET, init=False, repr=False)

    _CHANNEL_FILTER_SIZE: int = field(default=IndexMmapLayout.CHANNEL_FILTER_SIZE, init=False, repr=False)
    _CHANNEL_FILTER_CHANNEL_INDEX_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_FILTER_CHANNEL_INDEX_OFFSET, init=False, repr=False)
    _CHANNEL_FILTER_CHANNEL_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_FILTER_CHANNEL_OFFSET, init=False, repr=False)
    _CHANNEL_FILTER_CHANNEL_CAPACITY: int = field(default=IndexMmapLayout.CHANNEL_FILTER_CHANNEL_CAPACITY, init=False, repr=False)
    _CHANNEL_FILTER_ROW_OFFSET_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_FILTER_ROW_OFFSET_OFFSET, init=False, repr=False)
    _CHANNEL_FILTER_COUNT_OFFSET: int = field(default=IndexMmapLayout.CHANNEL_FILTER_COUNT_OFFSET, init=False, repr=False)

    _DIRECTION_INDEX_HEADER_SIZE: int = field(default=IndexMmapLayout.DIRECTION_INDEX_HEADER_SIZE, init=False, repr=False)
    _DIRECTION_INDEX_DIRECTION_COUNT_OFFSET: int = field(default=IndexMmapLayout.DIRECTION_INDEX_HEADER_DIRECTION_COUNT_OFFSET, init=False, repr=False)
    _DIRECTION_INDEX_MAX_DIRECTIONS_OFFSET: int = field(default=IndexMmapLayout.DIRECTION_INDEX_HEADER_MAX_DIRECTIONS_OFFSET, init=False, repr=False)
    _DIRECTION_INDEX_MAX_ROW_POOL_SIZE_OFFSET: int = field(default=IndexMmapLayout.DIRECTION_INDEX_HEADER_MAX_ROW_POOL_SIZE_OFFSET, init=False, repr=False)

    _DIRECTION_FILTER_SIZE: int = field(default=IndexMmapLayout.DIRECTION_FILTER_SIZE, init=False, repr=False)
    _DIRECTION_FILTER_DIRECTION_OFFSET: int = field(default=IndexMmapLayout.DIRECTION_FILTER_DIRECTION_OFFSET, init=False, repr=False)
    _DIRECTION_FILTER_ROW_OFFSET_OFFSET: int = field(default=IndexMmapLayout.DIRECTION_FILTER_ROW_OFFSET_OFFSET, init=False, repr=False)
    _DIRECTION_FILTER_COUNT_OFFSET: int = field(default=IndexMmapLayout.DIRECTION_FILTER_COUNT_OFFSET, init=False, repr=False)
    _multi_can_merge_state: Dict[Tuple[bool, Tuple[int, ...]], Dict[str, Any]] = field(default_factory=dict, init=False, repr=False)

    # Lightweight catalog: can_id → list of per-segment descriptors.
    # Each descriptor = (seg_path, row_pool_base, row_pool_off, count,
    #                     changed_pool_base, changed_pool_off, changed_count)
    # Only filter metadata is read — NO row data loaded into RAM.
    _can_id_catalog: Dict[int, List[tuple]] = field(default_factory=dict, init=False, repr=False)
    _can_id_timestamp_bounds: Dict[int, Tuple[float, float]] = field(default_factory=dict, init=False, repr=False)
    _global_timestamp_bounds: Optional[Tuple[float, float]] = field(default=None, init=False, repr=False)
    _channel_catalog: Dict[str, List[tuple]] = field(default_factory=dict, init=False, repr=False)
    _direction_catalog: Dict[str, List[tuple]] = field(default_factory=dict, init=False, repr=False)
    _multi_channel_merge_state: Dict[Tuple[str, ...], Dict[str, Any]] = field(default_factory=dict, init=False, repr=False)
    _multi_direction_merge_state: Dict[Tuple[str, ...], Dict[str, Any]] = field(default_factory=dict, init=False, repr=False)
    _native_reader: Optional[DataMmapManagerClient] = field(default=None, init=False, repr=False)

    def __post_init__(self) -> None:
        MmapHeaderConstract.load_from_native_binding()
        ParsedEntryLayout.load_from_native_binding()
        IndexMmapLayout.load_from_native_binding()

        self._DATA_HEADER_SIZE = int(ParsedEntryLayout.DATA_HEADER_SIZE)
        self._ENTRY_SIZE = int(ParsedEntryLayout.ENTRY_SIZE)
        self._ENTRY_LINE_NUMBER_OFFSET = int(ParsedEntryLayout.LINE_NUMBER_OFFSET)
        self._ENTRY_TIMESTAMP_OFFSET = int(ParsedEntryLayout.TIMESTAMP_OFFSET)
        self._ENTRY_LAST_TIMESTAMP_OFFSET = int(ParsedEntryLayout.LAST_TIMESTAMP_OFFSET)
        self._ENTRY_CAN_ID_OFFSET = int(ParsedEntryLayout.CAN_ID_OFFSET)
        self._ENTRY_DIRECTION_OFFSET = int(ParsedEntryLayout.DIRECTION_OFFSET)
        self._ENTRY_DATA_LEN_OFFSET = int(ParsedEntryLayout.DATA_LEN_OFFSET)
        self._ENTRY_CHANGED_OFFSET = int(ParsedEntryLayout.CHANGED_OFFSET)
        self._ENTRY_DATA_OFFSET = int(ParsedEntryLayout.DATA_OFFSET)
        self._ENTRY_DATA_CAPACITY = int(ParsedEntryLayout.DATA_CAPACITY)
        self._ENTRY_CHANNEL_OFFSET = int(ParsedEntryLayout.CHANNEL_OFFSET)
        self._ENTRY_CHANNEL_CAPACITY = int(ParsedEntryLayout.CHANNEL_CAPACITY)

        self._INDEX_HEADER_SIZE = int(IndexMmapLayout.INDEX_HEADER_SIZE)
        self._INDEX_CAN_ID_COUNT_OFFSET = int(IndexMmapLayout.INDEX_HEADER_CAN_ID_COUNT_OFFSET)
        self._INDEX_MAX_CAN_IDS_OFFSET = int(IndexMmapLayout.INDEX_HEADER_MAX_CAN_IDS_OFFSET)
        self._INDEX_MAX_ROW_POOL_SIZE_OFFSET = int(IndexMmapLayout.INDEX_HEADER_MAX_ROW_POOL_SIZE_OFFSET)
        self._INDEX_MAX_CHANGED_ROW_POOL_SIZE_OFFSET = int(IndexMmapLayout.INDEX_HEADER_MAX_CHANGED_ROW_POOL_SIZE_OFFSET)
        self._INDEX_MAX_TS_POOL_SIZE_OFFSET = int(IndexMmapLayout.INDEX_HEADER_MAX_TS_POOL_SIZE_OFFSET)

        self._INDEX_FILTER_SIZE = int(IndexMmapLayout.CAN_ID_FILTER_SIZE)
        self._INDEX_FILTER_CAN_ID_OFFSET = int(IndexMmapLayout.CAN_ID_FILTER_CAN_ID_OFFSET)
        self._INDEX_FILTER_ROW_OFFSET_OFFSET = int(IndexMmapLayout.CAN_ID_FILTER_ROW_OFFSET_OFFSET)
        self._INDEX_FILTER_CHANGED_ROW_OFFSET_OFFSET = int(IndexMmapLayout.CAN_ID_FILTER_CHANGED_ROW_OFFSET_OFFSET)
        self._INDEX_FILTER_TS_OFFSET_OFFSET = int(IndexMmapLayout.CAN_ID_FILTER_TS_OFFSET_OFFSET)
        self._INDEX_FILTER_COUNT_OFFSET = int(IndexMmapLayout.CAN_ID_FILTER_COUNT_OFFSET)
        self._INDEX_FILTER_CHANGED_COUNT_OFFSET = int(IndexMmapLayout.CAN_ID_FILTER_CHANGED_COUNT_OFFSET)

        self._CHANNEL_INDEX_HEADER_SIZE = int(IndexMmapLayout.CHANNEL_INDEX_HEADER_SIZE)
        self._CHANNEL_INDEX_CHANNEL_COUNT_OFFSET = int(IndexMmapLayout.CHANNEL_INDEX_HEADER_CHANNEL_COUNT_OFFSET)
        self._CHANNEL_INDEX_MAX_CHANNELS_OFFSET = int(IndexMmapLayout.CHANNEL_INDEX_HEADER_MAX_CHANNELS_OFFSET)
        self._CHANNEL_INDEX_MAX_ROW_POOL_SIZE_OFFSET = int(IndexMmapLayout.CHANNEL_INDEX_HEADER_MAX_ROW_POOL_SIZE_OFFSET)

        self._CHANNEL_FILTER_SIZE = int(IndexMmapLayout.CHANNEL_FILTER_SIZE)
        self._CHANNEL_FILTER_CHANNEL_INDEX_OFFSET = int(IndexMmapLayout.CHANNEL_FILTER_CHANNEL_INDEX_OFFSET)
        self._CHANNEL_FILTER_CHANNEL_OFFSET = int(IndexMmapLayout.CHANNEL_FILTER_CHANNEL_OFFSET)
        self._CHANNEL_FILTER_CHANNEL_CAPACITY = int(IndexMmapLayout.CHANNEL_FILTER_CHANNEL_CAPACITY)
        self._CHANNEL_FILTER_ROW_OFFSET_OFFSET = int(IndexMmapLayout.CHANNEL_FILTER_ROW_OFFSET_OFFSET)
        self._CHANNEL_FILTER_COUNT_OFFSET = int(IndexMmapLayout.CHANNEL_FILTER_COUNT_OFFSET)

        self._DIRECTION_INDEX_HEADER_SIZE = int(IndexMmapLayout.DIRECTION_INDEX_HEADER_SIZE)
        self._DIRECTION_INDEX_DIRECTION_COUNT_OFFSET = int(IndexMmapLayout.DIRECTION_INDEX_HEADER_DIRECTION_COUNT_OFFSET)
        self._DIRECTION_INDEX_MAX_DIRECTIONS_OFFSET = int(IndexMmapLayout.DIRECTION_INDEX_HEADER_MAX_DIRECTIONS_OFFSET)
        self._DIRECTION_INDEX_MAX_ROW_POOL_SIZE_OFFSET = int(IndexMmapLayout.DIRECTION_INDEX_HEADER_MAX_ROW_POOL_SIZE_OFFSET)

        self._DIRECTION_FILTER_SIZE = int(IndexMmapLayout.DIRECTION_FILTER_SIZE)
        self._DIRECTION_FILTER_DIRECTION_OFFSET = int(IndexMmapLayout.DIRECTION_FILTER_DIRECTION_OFFSET)
        self._DIRECTION_FILTER_ROW_OFFSET_OFFSET = int(IndexMmapLayout.DIRECTION_FILTER_ROW_OFFSET_OFFSET)
        self._DIRECTION_FILTER_COUNT_OFFSET = int(IndexMmapLayout.DIRECTION_FILTER_COUNT_OFFSET)

        # manifest object must be provided; prefer factory `from_manifest_file`
        if not isinstance(self.manifest, MmapFamilyManifest):
            raise ValueError("manifest must be a MmapFamilyManifest instance")

        # Derived/runtime-backed fields (use properties to expose)
        self._total_lines: int = 0
        self._mmap_file_count: int = 0
        try:
            self._mmap_capacity: int = int(getattr(self.manifest, "mmap_capacity", 0)) or 1_000_000
        except Exception:
            self._mmap_capacity = 1_000_000

    @property
    def manifest_path(self) -> Path:
        if self._manifest_path is not None:
            return self._manifest_path
        raise ValueError("manifest_path not available for this instance")

    @manifest_path.setter
    def manifest_path(self, value: Path) -> None:
        self._manifest_path = Path(value)

    @classmethod
    def from_manifest_file(cls, path: Path) -> "CANLogRawDiskFile":
        manifest = MmapFamilyManifest.load(path)
        if manifest is None:
            raise FileNotFoundError(path)
        inst = cls(manifest=manifest)
        inst._manifest_path = Path(path)
        return inst

    @property
    def record_dir(self) -> Path:
        return self.manifest_path.parent

    def _load_manifest(self) -> Optional[MmapFamilyManifest]:
        # legacy loader removed; instances carry a manifest object
        raise NotImplementedError("use CANLogRawDiskFile.from_manifest_file to construct from a file")

    def _ensure_native_reader(self, segs: Optional[List[Path]] = None) -> Optional[DataMmapManagerClient]:
        paths = segs if segs is not None else self.data_segment_paths()
        if not paths:
            return None
        path_strs = [str(path) for path in paths]
        capacity = int(self.mmap_capacity)
        if self._native_reader is None:
            try:
                self._native_reader = DataMmapManagerClient(path_strs, capacity)
            except Exception:
                self._native_reader = None
                return None
        else:
            self._native_reader.configure_reader(path_strs, capacity)
        return self._native_reader

    # Expose derived/runtime fields as properties. They are cached but
    # refreshed from disk when not available or when refresh is explicitly called.
    @property
    def total_lines(self) -> int:
        if int(self._total_lines) <= 0:
            # ensure latest runtime info
            try:
                self.refresh_mmap_runtime()
            except Exception:
                pass
        return int(self._total_lines)

    @total_lines.setter
    def total_lines(self, value: int) -> None:
        self._total_lines = int(value or 0)

    @property
    def mmap_file_count(self) -> int:
        if int(self._mmap_file_count) <= 0:
            try:
                self.refresh_mmap_runtime()
            except Exception:
                pass
        return int(self._mmap_file_count)

    @mmap_file_count.setter
    def mmap_file_count(self, value: int) -> None:
        self._mmap_file_count = int(value or 0)

    @property
    def mmap_capacity(self) -> int:
        if int(self._mmap_capacity) <= 0:
            try:
                self.refresh_mmap_runtime()
            except Exception:
                pass
        return int(self._mmap_capacity)

    @mmap_capacity.setter
    def mmap_capacity(self, value: int) -> None:
        self._mmap_capacity = int(value or 0)

    def _manifest_segments_to_paths(self, segment_names: list[str]) -> list[Path]:
        result: list[Path] = []
        for name in segment_names:
            path = Path(name)
            result.append(path if path.is_absolute() else self.record_dir / path)
        return result

    @property
    def data_mmap_path(self) -> str:
        return self.data_segment_paths()

    @property
    def index_mmap_path(self) -> str:
        return self.index_segment_paths()

    @property
    def channel_index_mmap_path(self) -> str:
        return self.channel_index_segment_paths()

    @property
    def direction_index_mmap_path(self) -> str:
        return self.direction_index_segment_paths()

    # ────────────────────────────────────────────────────────────────────
    #  Mmap path management
    # ────────────────────────────────────────────────────────────────────
    def data_segment_paths(self) -> List[Path]:
        return self._manifest_segments_to_paths(self.manifest.data_segments)

    def index_segment_paths(self) -> List[Path]:
        return self._manifest_segments_to_paths(self.manifest.index_segments)

    def channel_index_segment_paths(self) -> List[Path]:
        return self._manifest_segments_to_paths(self.manifest.channel_index_segments)

    def direction_index_segment_paths(self) -> List[Path]:
        return self._manifest_segments_to_paths(self.manifest.direction_index_segments)
    
    # ────────────────────────────────────────────────────────────────────
    #  API for filter rows
    # ────────────────────────────────────────────────────────────────────
    def get_page_from_row_indices(self, first_line: int, page_size: int) -> List[ParsedEntry]:
        start = max(0, int(first_line))
        end = start + max(0, int(page_size))
        return self.get_messages_by_row_indices(range(start, end))

    def get_page_from_can_id_row_indices(self, can_id: int, first_line: int, page_size: int) -> List[ParsedEntry]:
        page_rows = self._read_row_page_from_mmap(can_id, first_line, page_size)
        return self.get_messages_by_row_indices(page_rows)

    def get_page_from_can_ids_row_indices(self, can_ids: List[int], first_line: int, page_size: int) -> List[ParsedEntry]:
        merged = self._merge_can_ids_page_from_mmap(can_ids, first_line, page_size, changed=False)
        return self.get_messages_by_row_indices(merged)

    def get_page_from_can_id_changed_row_indices(self, can_id: int, first_line: int, page_size: int) -> List[ParsedEntry]:
        page_rows = self._read_changed_row_page_from_mmap(can_id, first_line, page_size)
        return self.get_messages_by_row_indices(page_rows)

    def get_page_from_can_ids_changed_row_indices(self, can_ids: List[int], first_line: int, page_size: int) -> List[ParsedEntry]:
        merged = self._merge_can_ids_page_from_mmap(can_ids, first_line, page_size, changed=True)
        return self.get_messages_by_row_indices(merged)

    def get_page_from_channel_row_indices(self, channel: str, first_line: int, page_size: int) -> List[ParsedEntry]:
        page_rows = self._read_channel_row_page_from_mmap(channel, first_line, page_size)
        return self.get_messages_by_row_indices(page_rows)

    def get_page_from_channels_row_indices(self, channels: List[str], first_line: int, page_size: int) -> List[ParsedEntry]:
        merged = self._merge_channels_page_from_mmap(channels, first_line, page_size)
        return self.get_messages_by_row_indices(merged)

    def get_page_from_direction_row_indices(self, direction: str, first_line: int, page_size: int) -> List[ParsedEntry]:
        page_rows = self._read_direction_row_page_from_mmap(direction, first_line, page_size)
        return self.get_messages_by_row_indices(page_rows)

    def get_page_from_directions_row_indices(self, directions: List[str], first_line: int, page_size: int) -> List[ParsedEntry]:
        merged = self._merge_directions_page_from_mmap(directions, first_line, page_size)
        return self.get_messages_by_row_indices(merged)

    def get_page_from_timestamp_range(self,from_t: float,to_t: float,first_line: int,page_size: int,) -> List[ParsedEntry]:
        lo_t = float(from_t)
        hi_t = float(to_t)
        if lo_t > hi_t:
            lo_t, hi_t = hi_t, lo_t

        start_row = self.get_start_row_by_timestamp(lo_t)
        end_row = self.get_end_row_by_timestamp(hi_t)
        if end_row <= start_row:
            return []

        offset = max(0, int(first_line))
        size = max(0, int(page_size))
        if size == 0:
            return []

        window_total = end_row - start_row
        if offset >= window_total:
            return []

        global_first = start_row + offset
        take = min(size, window_total - offset)
        return self.get_page_from_row_indices(global_first, take)
    
    # ────────────────────────────────────────────────────────────────────
    #  API for row
    # ────────────────────────────────────────────────────────────────────
    def get_start_row_by_timestamp(self, timestamp: float) -> int:
        if self.total_lines <= 0:
            self.refresh_mmap_runtime()
        total = int(self.total_lines)
        if total <= 0:
            return 0

        segs = self.data_segment_paths()
        if not segs:
            return 0
        return self._timestamp_lower_bound_global(segs, total, float(timestamp))

    def get_start_row_by_can_id_timestamp(self, can_id: int, timestamp: float) -> int:
        total = self.get_total_count_by_can_id(int(can_id))
        if total <= 0:
            return 0
        return self._timestamp_lower_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_row_index_at_pos_can_id(int(can_id), pos),
            target_ts=float(timestamp),
        )

    def get_start_row_by_channel_timestamp(self, channel: str, timestamp: float) -> int:
        total = self.get_total_count_by_channel(channel)
        if total <= 0:
            return 0
        return self._timestamp_lower_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_row_index_at_pos_channel(channel, pos),
            target_ts=float(timestamp),
        )

    def get_start_row_by_direction_timestamp(self, direction: str, timestamp: float) -> int:
        total = self.get_total_count_by_direction(direction)
        if total <= 0:
            return 0
        return self._timestamp_lower_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_row_index_at_pos_direction(direction, pos),
            target_ts=float(timestamp),
        )

    def get_end_row_by_timestamp(self, timestamp: float) -> int:
        """Upper-bound end row (first row with timestamp > target) in global space."""
        if self.total_lines <= 0:
            self.refresh_mmap_runtime()
        total = int(self.total_lines)
        if total <= 0:
            return 0

        segs = self.data_segment_paths()
        if not segs:
            return 0
        return self._timestamp_upper_bound_global(segs, total, float(timestamp))

    def get_end_row_by_can_id_timestamp(self, can_id: int, timestamp: float) -> int:
        """Upper-bound end row in CAN-ID filtered space."""
        total = self.get_total_count_by_can_id(int(can_id))
        if total <= 0:
            return 0
        return self._timestamp_upper_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_row_index_at_pos_can_id(int(can_id), pos),
            target_ts=float(timestamp),
        )

    def get_end_row_by_channel_timestamp(self, channel: str, timestamp: float) -> int:
        """Upper-bound end row in channel filtered space."""
        total = self.get_total_count_by_channel(channel)
        if total <= 0:
            return 0
        return self._timestamp_upper_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_row_index_at_pos_channel(channel, pos),
            target_ts=float(timestamp),
        )

    def get_end_row_by_direction_timestamp(self, direction: str, timestamp: float) -> int:
        """Upper-bound end row in direction filtered space."""
        total = self.get_total_count_by_direction(direction)
        if total <= 0:
            return 0
        return self._timestamp_upper_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_row_index_at_pos_direction(direction, pos),
            target_ts=float(timestamp),
        )

    def get_start_row_by_can_id_changed_timestamp(self, can_id: int, timestamp: float) -> int:
        """Lower-bound start row in CAN-ID changed-only filtered space."""
        total = self.get_changed_count_by_can_id(int(can_id))
        if total <= 0:
            return 0
        return self._timestamp_lower_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_changed_row_index_at_pos_can_id(int(can_id), pos),
            target_ts=float(timestamp),
        )

    def get_end_row_by_can_id_changed_timestamp(self, can_id: int, timestamp: float) -> int:
        """Upper-bound end row in CAN-ID changed-only filtered space."""
        total = self.get_changed_count_by_can_id(int(can_id))
        if total <= 0:
            return 0
        return self._timestamp_upper_bound_indexed(
            total_count=total,
            read_row_index_at_pos=lambda pos: self._read_changed_row_index_at_pos_can_id(int(can_id), pos),
            target_ts=float(timestamp),
        )
    
    # ────────────────────────────────────────────────────────────────────
    #  API for size
    # ────────────────────────────────────────────────────────────────────
    def get_total_count_by_can_id(self, can_id: int) -> int:
        """Total row count for one CAN-ID (all + unchanged + changed)."""
        self._ensure_can_id_catalog()
        segs = self._can_id_catalog.get(int(can_id), [])
        return sum(c for _, _, _, c, _, _, _ in segs)

    def get_changed_count_by_can_id(self, can_id: int) -> int:
        """Changed-row count for one CAN-ID."""
        self._ensure_can_id_catalog()
        segs = self._can_id_catalog.get(int(can_id), [])
        return sum(cc for _, _, _, _, _, _, cc in segs)

    def get_total_count_by_can_ids(self, can_ids: List[int]) -> int:
        """Total row count across multiple CAN-IDs (sum, not merged)."""
        self._ensure_can_id_catalog()
        total = 0
        seen: Set[int] = set()
        for cid_raw in can_ids:
            cid = int(cid_raw)
            if cid in seen:
                continue
            seen.add(cid)
            segs = self._can_id_catalog.get(cid, [])
            total += sum(c for _, _, _, c, _, _, _ in segs)
        return total

    def get_changed_count_by_can_ids(self, can_ids: List[int]) -> int:
        """Changed-row count across multiple CAN-IDs."""
        self._ensure_can_id_catalog()
        total = 0
        seen: Set[int] = set()
        for cid_raw in can_ids:
            cid = int(cid_raw)
            if cid in seen:
                continue
            seen.add(cid)
            segs = self._can_id_catalog.get(cid, [])
            total += sum(cc for _, _, _, _, _, _, cc in segs)
        return total

    def get_total_count_by_channel(self, channel: str) -> int:
        self._ensure_channel_catalog()
        segs = self._channel_catalog.get(str(channel).lower(), [])
        return sum(c for _, _, _, c, _ in segs)

    def get_total_count_by_channels(self, channels: List[str]) -> int:
        self._ensure_channel_catalog()
        total = 0
        seen: Set[str] = set()
        for channel in channels:
            key = str(channel).lower()
            if key in seen:
                continue
            seen.add(key)
            segs = self._channel_catalog.get(key, [])
            total += sum(c for _, _, _, c, _ in segs)
        return total

    def get_total_count_by_direction(self, direction: str) -> int:
        self._ensure_direction_catalog()
        segs = self._direction_catalog.get(self._normalize_direction_key(direction), [])
        return sum(c for _, _, _, c, _ in segs)

    def get_total_count_by_directions(self, directions: List[str]) -> int:
        self._ensure_direction_catalog()
        total = 0
        seen: Set[str] = set()
        for direction in directions:
            key = self._normalize_direction_key(direction)
            if key in seen:
                continue
            seen.add(key)
            segs = self._direction_catalog.get(key, [])
            total += sum(c for _, _, _, c, _ in segs)
        return total

    # ────────────────────────────────────────────────────────────────────
    #  API for timestamp
    # ────────────────────────────────────────────────────────────────────

    def get_first_last_timestamp(self) -> Tuple[Optional[float], Optional[float]]:
        if self._global_timestamp_bounds is not None:
            return self._global_timestamp_bounds

        if self.total_lines <= 0:
            self.refresh_mmap_runtime()
        if self.total_lines <= 0:
            return None, None

        segs = self.data_segment_paths()
        if not segs:
            return None, None

        first_entry = self._read_entry_by_global_row(segs, 0)
        last_entry = self._read_entry_by_global_row(segs, int(self.total_lines) - 1)
        if first_entry is None or last_entry is None:
            return None, None

        self._global_timestamp_bounds = (float(first_entry.timestamp), float(last_entry.timestamp))
        return self._global_timestamp_bounds

    def get_first_last_timestamp_by_can_id(self, can_id: int) -> Tuple[Optional[float], Optional[float]]:
        self._ensure_can_id_catalog()
        bounds = self._can_id_timestamp_bounds.get(int(can_id))
        if bounds is None:
            return None, None
        return float(bounds[0]), float(bounds[1])

    def get_first_last_timestamp_by_can_ids(self, can_ids: List[int]) -> Tuple[Optional[float], Optional[float]]:
        self._ensure_can_id_catalog()

        seen: Set[int] = set()
        first_ts: Optional[float] = None
        last_ts: Optional[float] = None
        for can_id in can_ids:
            cid = int(can_id)
            if cid in seen:
                continue
            seen.add(cid)
            bounds = self._can_id_timestamp_bounds.get(cid)
            if bounds is None:
                continue
            f, l = float(bounds[0]), float(bounds[1])
            first_ts = f if first_ts is None else min(first_ts, f)
            last_ts = l if last_ts is None else max(last_ts, l)

        return first_ts, last_ts

    def get_timestamps_by_can_id(self, can_id: int) -> List[float]:
        rows = self.get_row_indices_by_list_id([int(can_id)])
        if not rows:
            return []

        segs = self.data_segment_paths()
        if not segs:
            return []

        timestamps: List[float] = []
        for row in rows:
            ts = self._read_timestamp_by_global_row_cached(segs, int(row))
            if ts is not None:
                timestamps.append(float(ts))

        return timestamps

    def get_timestamp_by_row(self, row_index: int) -> Optional[float]:
        """Timestamp for a global row index (0-based)."""
        if self.total_lines <= 0:
            self.refresh_mmap_runtime()
        if self.total_lines <= 0:
            return None

        row = int(row_index)
        if row < 0 or row >= int(self.total_lines):
            return None

        segs = self.data_segment_paths()
        if not segs:
            return None

        entry = self._read_entry_by_global_row(segs, row)
        if entry is None:
            return None
        return float(entry.timestamp)

    def get_timestamp_by_can_id_row(
        self,
        can_id: int,
        row_index: int,
        changed: bool = False,
    ) -> Optional[float]:
        """Timestamp for a row index within one CAN-ID filtered space (0-based)."""
        row = int(row_index)
        if row < 0:
            return None

        if changed:
            rows = self._read_changed_row_page_from_mmap(int(can_id), row, 1)
        else:
            rows = self._read_row_page_from_mmap(int(can_id), row, 1)
        if not rows:
            return None

        return self.get_timestamp_by_row(int(rows[0]))

    def get_timestamp_by_can_ids_row(
        self,
        can_ids: List[int],
        row_index: int,
        changed: bool = False,
    ) -> Optional[float]:
        """Timestamp for a row index within merged CAN-IDs filtered space (0-based)."""
        row = int(row_index)
        if row < 0:
            return None

        rows = self._merge_can_ids_page_from_mmap(
            can_ids=can_ids,
            first_line=row,
            page_size=1,
            changed=changed,
        )
        if not rows:
            return None

        return self.get_timestamp_by_row(int(rows[0]))

    def get_timestamp_by_channel_row(self, channel: str, row_index: int) -> Optional[float]:
        row = int(row_index)
        if row < 0:
            return None

        rows = self._read_channel_row_page_from_mmap(channel, row, 1)
        if not rows:
            return None
        return self.get_timestamp_by_row(int(rows[0]))

    ############################# Internal #################################    
    def _read_timestamp_by_global_row_cached(
        self,
        segs: List[Path],
        global_row: int,
    ) -> Optional[float]:
        manager = self._ensure_native_reader(segs)
        if manager is None:
            return None
        row = int(global_row)
        if row < 0:
            return None
        return manager.read_timestamp(row)

    def _timestamp_lower_bound_global(
        self,
        segs: List[Path],
        total: int,
        target_ts: float,
    ) -> int:
        manager = self._ensure_native_reader(segs)
        if manager is not None:
            return manager.timestamp_lower_bound(int(total), float(target_ts))
        return 0

    def _timestamp_upper_bound_global(
        self,
        segs: List[Path],
        total: int,
        target_ts: float,
    ) -> int:
        manager = self._ensure_native_reader(segs)
        if manager is not None:
            return manager.timestamp_upper_bound(int(total), float(target_ts))
        return 0

    def _read_row_index_at_pos_can_id(self, can_id: int, pos: int) -> Optional[int]:
        rows = self._read_row_page_from_mmap(int(can_id), int(pos), 1)
        return int(rows[0]) if rows else None

    def _read_row_index_at_pos_channel(self, channel: str, pos: int) -> Optional[int]:
        rows = self._read_channel_row_page_from_mmap(channel, int(pos), 1)
        return int(rows[0]) if rows else None

    def _read_row_index_at_pos_direction(self, direction: str, pos: int) -> Optional[int]:
        rows = self._read_direction_row_page_from_mmap(direction, int(pos), 1)
        return int(rows[0]) if rows else None

    def _read_changed_row_index_at_pos_can_id(self, can_id: int, pos: int) -> Optional[int]:
        rows = self._read_changed_row_page_from_mmap(int(can_id), int(pos), 1)
        return int(rows[0]) if rows else None

    def _timestamp_lower_bound_indexed(
        self,
        total_count: int,
        read_row_index_at_pos: Callable[[int], Optional[int]],
        target_ts: float,
    ) -> int:
        total = int(total_count)
        if total <= 0:
            return 0

        segs = self.data_segment_paths()
        if not segs:
            return 0

        def ts_by_pos(pos: int) -> float:
            row = read_row_index_at_pos(pos)
            if row is None:
                return float("inf")
            ts = self._read_timestamp_by_global_row_cached(segs, row)
            return float("inf") if ts is None else float(ts)

        lo, hi = 0, total
        while lo < hi:
            mid = (lo + hi) // 2
            if ts_by_pos(mid) < float(target_ts):
                lo = mid + 1
            else:
                hi = mid
        return lo

    def _timestamp_upper_bound_indexed(
        self,
        total_count: int,
        read_row_index_at_pos: Callable[[int], Optional[int]],
        target_ts: float,
    ) -> int:
        total = int(total_count)
        if total <= 0:
            return 0

        segs = self.data_segment_paths()
        if not segs:
            return 0

        def ts_by_pos(pos: int) -> float:
            row = read_row_index_at_pos(pos)
            if row is None:
                return float("inf")
            ts = self._read_timestamp_by_global_row_cached(segs, row)
            return float("inf") if ts is None else float(ts)

        lo, hi = 0, total
        while lo < hi:
            mid = (lo + hi) // 2
            if ts_by_pos(mid) <= float(target_ts):
                lo = mid + 1
            else:
                hi = mid
        return lo


    def refresh_can_ids_runtime(self):
        # Clear lightweight catalog so it re-scans on next demand
        self._can_id_catalog.clear()
        self._can_id_timestamp_bounds.clear()
        self._global_timestamp_bounds = None
        self._channel_catalog.clear()
        self._direction_catalog.clear()
        self.channels = []
        # Clear cursor states for multi-CAN pagination
        self._multi_can_merge_state.clear()
        self._multi_channel_merge_state.clear()
        self._multi_direction_merge_state.clear()
        # Rebuild catalog (cheap — only filter metadata, no row data)
        self._ensure_can_id_catalog()
        self._ensure_channel_catalog()
        self._ensure_direction_catalog()


    def _read_segment_write_count(self, seg_path: Path) -> int:
        try:
            return int(DataMmapManagerClient.read_segment_write_count(str(seg_path)))
        except Exception:
            return 0

    def _read_segment_capacity(self, seg_path: Path) -> int:
        try:
            cap = int(DataMmapManagerClient.read_segment_capacity(str(seg_path)))
            return cap if cap > 0 else self.mmap_capacity
        except Exception:
            return self.mmap_capacity

    def refresh_mmap_runtime(self):
        segs = self.data_segment_paths()
        # Update cached runtime values
        self._mmap_file_count = len(segs)
        if segs:
            self._mmap_capacity = self._read_segment_capacity(segs[0])
        manager = self._ensure_native_reader(segs)
        if manager is not None:
            self._total_lines = manager.read_total_rows()
            return
        self._total_lines = sum(self._read_segment_write_count(seg) for seg in segs)


    def _read_entry_from_segment(self, seg_path: Path, local_idx: int) -> Optional[ParsedEntry]:
        try:
            segs = [Path(seg_path)]
            return self._read_entry_by_global_row(segs, int(local_idx))
        except Exception:
            return None

    def get_page_lines(self, first_line: int, page_size: int) -> List[ParsedEntry]:
        start = max(0, int(first_line))
        end = start + max(0, int(page_size))
        return self.get_messages_by_row_indices(range(start, end))

    def get_all_can_ids(self) -> List[int]:
        self._ensure_can_id_catalog()
        return self.can_ids

    def get_all_channels(self) -> List[str]:
        self._ensure_channel_catalog()
        return self.channels
    
    def get_all_lines(self) -> List[ParsedEntry]:
        return self.get_page_lines(0, 20_000)

    def get_row_indices_by_list_id(self, can_ids: List[int]) -> List[int]:
        result: List[int] = []
        for can_id in can_ids:
            cid = int(can_id)
            total = self.get_total_count_by_can_id(cid)
            if total > 0:
                result.extend(self._read_row_page_from_mmap(cid, 0, total))
        return result

    def get_row_indices_by_channel(self, channel: str) -> List[int]:
        total = self.get_total_count_by_channel(channel)
        if total <= 0:
            return []
        return self._read_channel_row_page_from_mmap(channel, 0, total)

    def get_row_indices_by_direction(self, direction: str) -> List[int]:
        total = self.get_total_count_by_direction(direction)
        if total <= 0:
            return []
        return self._read_direction_row_page_from_mmap(direction, 0, total)

    def get_row_indices_by_directions(self, directions: List[str]) -> List[int]:
        total = self.get_total_count_by_directions(directions)
        if total <= 0:
            return []
        return self._merge_directions_page_from_mmap(directions, 0, total)

    def get_row_indices_by_channels(self, channels: List[str]) -> List[int]:
        total = self.get_total_count_by_channels(channels)
        if total <= 0:
            return []
        return self._merge_channels_page_from_mmap(channels, 0, total)

    def filter_row_indices_by_direction(self, direction: str, row_indices) -> List[int]:
        if row_indices is None:
            return self.get_row_indices_by_direction(direction)
        rows = list(row_indices)
        lines = self.get_messages_by_row_indices(rows)
        d = direction.lower()
        return [rows[i] for i, entry in enumerate(lines) if entry.direction_str.lower() == d]

    def filter_row_indices_by_channel(self, channel: str, row_indices) -> List[int]:
        rows = list(row_indices)
        lines = self.get_messages_by_row_indices(rows)
        target = str(channel).lower()
        return [rows[i] for i, entry in enumerate(lines) if entry.channel_str.lower() == target]

    def filter_row_indices_by_timestamp_range(self, from_t: float, to_t: float, row_indices) -> List[int]:
        rows = list(row_indices)
        if not rows:
            return []

        lo_t = float(from_t)
        hi_t = float(to_t)
        if lo_t > hi_t:
            lo_t, hi_t = hi_t, lo_t

        segs = self.data_segment_paths()
        if not segs:
            return []

        def ts_at_pos(pos: int) -> float:
            ts = self._read_timestamp_by_global_row_cached(segs, int(rows[pos]))
            return float("inf") if ts is None else float(ts)

        lo, hi = 0, len(rows)
        while lo < hi:
            mid = (lo + hi) // 2
            if ts_at_pos(mid) < lo_t:
                lo = mid + 1
            else:
                hi = mid
        start = lo

        lo, hi = start, len(rows)
        while lo < hi:
            mid = (lo + hi) // 2
            if ts_at_pos(mid) <= hi_t:
                lo = mid + 1
            else:
                hi = mid
        end = lo

        return rows[start:end]

