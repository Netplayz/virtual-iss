FROM python:3.13-slim-bookworm
WORKDIR /app
COPY orchestrator/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY orchestrator/ ./
COPY configs/ ./configs/
CMD ["python", "main.py"]
