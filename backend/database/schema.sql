-- ==========================================================
-- SEGUIMIENTO 360 - SISTEMA DE ALARMA Y MONITOREO
-- Esquema PostgreSQL v1.0
-- ==========================================================

-- ---------- TABLA: USUARIOS ----------
CREATE TABLE IF NOT EXISTS users (
    id SERIAL PRIMARY KEY,
    email VARCHAR(255) UNIQUE NOT NULL,
    password_hash VARCHAR(255) NOT NULL,
    full_name VARCHAR(255),
    phone VARCHAR(50),
    role VARCHAR(50) NOT NULL DEFAULT 'customer',
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP NOT NULL DEFAULT NOW()
);
-- role: customer | admin | super_admin

-- ---------- TABLA: TENANTS (clientes) ----------
CREATE TABLE IF NOT EXISTS tenants (
    id SERIAL PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    owner_user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE RESTRICT,
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

-- ---------- TABLA: SITIOS (propiedades) ----------
CREATE TABLE IF NOT EXISTS sites (
    id SERIAL PRIMARY KEY,
    tenant_id INTEGER NOT NULL REFERENCES tenants(id) ON DELETE CASCADE,
    name VARCHAR(255) NOT NULL,
    address TEXT,
    latitude DECIMAL(10,8),
    longitude DECIMAL(11,8),
    is_active BOOLEAN NOT NULL DEFAULT TRUE,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);

-- ---------- TABLA: DISPOSITIVOS ----------
CREATE TABLE IF NOT EXISTS devices (
    id SERIAL PRIMARY KEY,
    serial_number VARCHAR(100) UNIQUE NOT NULL,
    device_type VARCHAR(50) NOT NULL,
    firmware_version VARCHAR(50),
    tenant_id INTEGER REFERENCES tenants(id) ON DELETE SET NULL,
    site_id INTEGER REFERENCES sites(id) ON DELETE SET NULL,
    status VARCHAR(50) NOT NULL DEFAULT 'available',
    bound_at TIMESTAMP,
    unbound_at TIMESTAMP,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);
-- device_type: hub_s3 | sensor_magnetic | sensor_pir | sensor_smoke
--              sensor_temp | sensor_flood | siren | panic_button | vehicle_tracker
-- status: available | bound | inactive

-- ---------- TABLA: CREDENCIALES MQTT POR DISPOSITIVO ----------
CREATE TABLE IF NOT EXISTS device_credentials (
    id SERIAL PRIMARY KEY,
    device_id INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    mqtt_user VARCHAR(150) UNIQUE NOT NULL,
    mqtt_password_hash VARCHAR(255) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    rotated_at TIMESTAMP
);

-- ---------- TABLA: AUDITORIA DE DISPOSITIVOS ----------
CREATE TABLE IF NOT EXISTS device_audit_log (
    id SERIAL PRIMARY KEY,
    device_id INTEGER NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    tenant_id INTEGER,
    action VARCHAR(50) NOT NULL,
    performed_by INTEGER REFERENCES users(id),
    details JSONB,
    created_at TIMESTAMP NOT NULL DEFAULT NOW()
);
-- action: bind | unbind | transfer | factory_reset

-- ---------- INDICES ----------
CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);
CREATE INDEX IF NOT EXISTS idx_tenants_owner ON tenants(owner_user_id);
CREATE INDEX IF NOT EXISTS idx_sites_tenant ON sites(tenant_id);
CREATE INDEX IF NOT EXISTS idx_devices_serial ON devices(serial_number);
CREATE INDEX IF NOT EXISTS idx_devices_tenant ON devices(tenant_id);
CREATE INDEX IF NOT EXISTS idx_devices_status ON devices(status);
CREATE INDEX IF NOT EXISTS idx_audit_device ON device_audit_log(device_id);
CREATE INDEX IF NOT EXISTS idx_audit_created ON device_audit_log(created_at);
