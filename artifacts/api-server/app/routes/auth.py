from __future__ import annotations

import re
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel, Field
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from app.auth import get_current_admin, get_current_user, hash_password, issue_access_token, user_payload, verify_password
from app.database import get_db
from app.models import User

router = APIRouter(tags=["auth"])
_EMAIL = re.compile(r"^[^@\s]+@[^@\s]+\.[^@\s]+$")


class AuthRequest(BaseModel):
    email: str = Field(min_length=5, max_length=255)
    password: str = Field(min_length=8, max_length=256)


@router.post("/auth/register", status_code=201)
async def register(request: AuthRequest, db: AsyncSession = Depends(get_db)):
    email = request.email.strip().lower()
    if not _EMAIL.fullmatch(email):
        raise HTTPException(status_code=422, detail="A valid email address is required")
    existing = await db.execute(select(User).where(User.email == email))
    if existing.scalar_one_or_none() is not None:
        raise HTTPException(status_code=409, detail="An account with this email already exists")
    user = User(email=email, password_hash=hash_password(request.password), role="user")
    db.add(user)
    await db.commit()
    await db.refresh(user)
    return {"user": user_payload(user), "access_token": issue_access_token(user.id), "token_type": "bearer"}


@router.post("/auth/login")
async def login(request: AuthRequest, db: AsyncSession = Depends(get_db)):
    email = request.email.strip().lower()
    result = await db.execute(select(User).where(User.email == email))
    user = result.scalar_one_or_none()
    if user is None or not verify_password(request.password, user.password_hash):
        raise HTTPException(status_code=401, detail="Invalid email or password")
    return {"user": user_payload(user), "access_token": issue_access_token(user.id), "token_type": "bearer"}


@router.get("/auth/me")
async def me(user: User = Depends(get_current_user)):
    return user_payload(user)


@router.get("/auth/users")
async def list_users(_: User = Depends(get_current_admin), db: AsyncSession = Depends(get_db)):
    result = await db.execute(select(User).order_by(User.id.asc()))
    return [user_payload(user) for user in result.scalars().all()]
