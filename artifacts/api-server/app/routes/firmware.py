from __future__ import annotations

from fastapi import APIRouter, HTTPException

from app.firmware.jobs import FirmwareJobService
from app.firmware.models import FirmwareJobResponse, FirmwareRequest, TargetListResponse
from app.firmware.registry import list_targets

router = APIRouter(tags=["firmware"])
_service = FirmwareJobService()


@router.get("/firmware/targets", response_model=TargetListResponse)
async def get_firmware_targets() -> TargetListResponse:
    return TargetListResponse(targets=list_targets())


@router.post("/firmware/jobs", response_model=FirmwareJobResponse, status_code=201)
async def create_firmware_job(request: FirmwareRequest) -> FirmwareJobResponse:
    return FirmwareJobResponse(job=await _service.create(request))


@router.get("/firmware/jobs/{job_id}", response_model=FirmwareJobResponse)
async def get_firmware_job(job_id: str) -> FirmwareJobResponse:
    job = _service.store.get(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="Firmware job not found")
    return FirmwareJobResponse(job=job)
