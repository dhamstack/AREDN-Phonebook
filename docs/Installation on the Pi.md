## Installation of Docker

```
curl -sSL https://get.docker.com | sh
sudo usermod -aG docker $USER
sudo reboot
#Check:
docker compose version
```

## Directory Structure:

/home/$USER/AREDNmon/
├── docker-compose.yml  <-- CREATE HERE
├── collector/
│   ├── Dockerfile
│   ├── requirements.txt
│   └── app.py (Your Python code)
└── # ... other project files

`mkdir AREDNmon`
`cd AREDNmon`

## Collector Directory and Files:

```
# Create the main project root and navigate into it
mkdir AREDNmon
cd AREDNmon

# Create the subdirectory for the Python application and necessary files
mkdir collector
touch collector/requirements.txt
touch collector/Dockerfile 

# Create the Docker Compose file in the root directory
touch docker-compose.yml
```



### Navigate to Collector Directory

Move into the `collector` directory to populate the `Dockerfile` next.

```
cd collector
```

------



### Populate the Configuration Files

This step writes the content for your application blueprints. **Remember to change the password in the next section (3.3) before launching.**

### Collector Dockerfile (`collector/Dockerfile`)

This file defines how to build the Python Collector image. Run this while in the `collector/` directory.

```
cat << 'EOF' > Dockerfile
# Use a multi-arch Python base image for ARM (arm64v8 is preferred for 64-bit OS)
FROM python:3.11-slim-bookworm

# Set the working directory in the container
WORKDIR /app

# Copy the requirements file and install dependencies
COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy the entire collector application source code
COPY . .

# Expose the port your Flask/Python web server uses (e.g., 5000 for the API)
EXPOSE 5000

# Command to run the application (replace 'app.py' with your main entry file)
CMD ["python", "app.py"]
EOF
```

### Move to Project Root

Move back to the project root directory (`AREDNmon/`) to create the compose file.

```
cd ..
```

### Compose Configuration (`docker-compose.yml`)

Define your password and then write the content for the `docker-compose.yml` file.

```

version: '3.8'

services:
  # 1. InfluxDB Time-Series Service (v1.8 for OpenWISP compatibility)
  influxdb:
    # Use the 1.8 tag for stability and OpenWISP compatibility on ARM
    image: influxdb:1.8-alpine
    container_name: arednmon_influxdb
    restart: always
    environment:
      # These set up the default database, admin user, and password
      INFLUXDB_DB: aredn_meshmon
      INFLUXDB_ADMIN_USER: aredn_user
      INFLUXDB_ADMIN_PASSWORD: juLian123
    volumes:
      # Persist InfluxDB metrics data
      - arednmon_influx_data:/var/lib/influxdb
    ports:
      - "8086:8086" # Standard InfluxDB HTTP port
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8086/ping"]
      interval: 5s
      timeout: 5s
      retries: 5

  # 2. Python Collector/Analyzer Service (hosts SQLite file)
  collector:
    build:
      context: ./collector
    container_name: arednmon_collector
    restart: unless-stopped
    depends_on:
      influxdb:
        condition: service_healthy
    environment:
      # InfluxDB connection details
      INFLUX_HOST: influxdb
      INFLUX_PORT: 8086
      INFLUX_DB: aredn_meshmon
      INFLUX_USER: aredn_user
      INFLUX_PASS: juLian123
      # SQLite path for Django/Relational data
      SQLITE_PATH: /app/data/db.sqlite3
      COLLECTOR_PORT: 5000
    volumes:
      # Persist the SQLite file in the 'data' folder
      - arednmon_sqlite_data:/app/data
    ports:
      - "8000:5000"

volumes:
  # Define volumes for persistence
  arednmon_influx_data:
    driver: local
  arednmon_sqlite_data:
    driver: local
```

### Launch and Verify Infrastructure

**Before proceeding:** Ensure your actual Python code (`app.py`) and required libraries (`requirements.txt`) are placed in the `collector/` directory.

### Build and Start Services

Run this command from the `AREDNmon/` root directory to launch the system.

```
docker compose up -d --build
```

### Verify Status

Check that both containers are running and healthy.

```
docker compose ps
```

The output should display both containers as 'Up' (healthy). Your infrastructure is now running.