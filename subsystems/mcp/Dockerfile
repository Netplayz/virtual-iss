FROM python:3.12-slim

WORKDIR /app
COPY subsystems/mcp/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY subsystems/mcp/ .

EXPOSE 8331
ENTRYPOINT ["python", "main.py"]
