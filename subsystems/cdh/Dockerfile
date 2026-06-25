FROM python:3.13-slim-bookworm
WORKDIR /app
COPY subsystems/cdh/requirements.txt ./
RUN pip install --no-cache-dir -r requirements.txt
COPY subsystems/cdh/ ./
CMD ["python", "main.py"]
