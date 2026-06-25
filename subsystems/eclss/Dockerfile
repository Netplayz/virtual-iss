FROM python:3.13-slim-bookworm
WORKDIR /app
COPY subsystems/eclss/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY subsystems/eclss/ ./
CMD ["python", "main.py"]
