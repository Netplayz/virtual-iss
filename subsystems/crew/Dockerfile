FROM python:3.13-slim-bookworm
WORKDIR /app
COPY subsystems/crew/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY subsystems/crew/ ./
CMD ["python", "main.py"]
