// ===== Registrar usuario =====
const pg = global.get('pg');
const bcrypt = global.get('bcrypt');
const jwt = global.get('jwt');
const dbConfig = global.get('dbConfig');
const jwtSecret = global.get('jwtSecret');
const client = new pg.Client(dbConfig);
try {
    const { email, password, full_name, phone } = msg.payload;
    if (!email || !password) {
        msg.statusCode = 400;
        msg.payload = { error: 'Email y password son requeridos' };
        return msg;
    }
    if (password.length < 8) {
        msg.statusCode = 400;
        msg.payload = { error: 'Password debe tener al menos 8 caracteres' };
        return msg;
    }
    const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
    if (!emailRegex.test(email)) {
        msg.statusCode = 400;
        msg.payload = { error: 'Email invalido' };
        return msg;
    }
    await client.connect();
    const existing = await client.query('SELECT id FROM users WHERE email = $1', [email.toLowerCase()]);
    if (existing.rows.length > 0) {
        msg.statusCode = 409;
        msg.payload = { error: 'El email ya esta registrado' };
        return msg;
    }
    const password_hash = await bcrypt.hash(password, 10);
    const userResult = await client.query('INSERT INTO users (email, password_hash, full_name, phone, role, is_active) VALUES ($1, $2, $3, $4, $5, true) RETURNING id, email, full_name, role', [email.toLowerCase(), password_hash, full_name || null, phone || null, 'customer']);
    const user = userResult.rows[0];
    const tenantResult = await client.query('INSERT INTO tenants (name, owner_user_id, is_active) VALUES ($1, $2, true) RETURNING id', [email.split('@')[0], user.id]);
    msg.statusCode = 201;
    msg.payload = { success: true, user: user, tenant_id: tenantResult.rows[0].id };
    return msg;
} catch (err) {
    node.error('Register: ' + err.message, msg);
    msg.statusCode = 500;
    msg.payload = { error: 'Error interno del servidor' };
    return msg;
} finally {
    client.end().catch(()=>{});
}

// ===== Login + JWT =====
const pg = global.get('pg');
const bcrypt = global.get('bcrypt');
const jwt = global.get('jwt');
const dbConfig = global.get('dbConfig');
const jwtSecret = global.get('jwtSecret');
const client = new pg.Client(dbConfig);
try {
    const { email, password } = msg.payload;
    if (!email || !password) {
        msg.statusCode = 400;
        msg.payload = { error: 'Email y password son requeridos' };
        return msg;
    }
    await client.connect();
    const result = await client.query('SELECT u.id, u.email, u.password_hash, u.full_name, u.phone, u.role, u.is_active, t.id as tenant_id FROM users u LEFT JOIN tenants t ON t.owner_user_id = u.id AND t.is_active = true WHERE u.email = $1', [email.toLowerCase()]);
    if (result.rows.length === 0) {
        msg.statusCode = 401;
        msg.payload = { error: 'Credenciales invalidas' };
        return msg;
    }
    const user = result.rows[0];
    if (!user.is_active) {
        msg.statusCode = 403;
        msg.payload = { error: 'Cuenta desactivada' };
        return msg;
    }
    const match = await bcrypt.compare(password, user.password_hash);
    if (!match) {
        msg.statusCode = 401;
        msg.payload = { error: 'Credenciales invalidas' };
        return msg;
    }
    const token = jwt.sign({ user_id: user.id, email: user.email, role: user.role, tenant_id: user.tenant_id }, jwtSecret, { expiresIn: '24h' });
    msg.statusCode = 200;
    msg.payload = { success: true, token: token, user: { id: user.id, email: user.email, full_name: user.full_name, role: user.role, tenant_id: user.tenant_id } };
    return msg;
} catch (err) {
    node.error('Login: ' + err.message, msg);
    msg.statusCode = 500;
    msg.payload = { error: 'Error interno del servidor' };
    return msg;
} finally {
    client.end().catch(()=>{});
}

// ===== Perfil con JWT =====
const pg = global.get('pg');
const jwt = global.get('jwt');
const dbConfig = global.get('dbConfig');
const jwtSecret = global.get('jwtSecret');
const client = new pg.Client(dbConfig);
try {
    const authHeader = msg.req.headers.authorization;
    if (!authHeader || !authHeader.startsWith('Bearer ')) {
        msg.statusCode = 401;
        msg.payload = { error: 'Token no proporcionado' };
        return msg;
    }
    const token = authHeader.substring(7);
    let decoded;
    try {
        decoded = jwt.verify(token, jwtSecret);
    } catch (e) {
        msg.statusCode = 401;
        msg.payload = { error: 'Token invalido o expirado' };
        return msg;
    }
    await client.connect();
    const result = await client.query('SELECT u.id, u.email, u.full_name, u.phone, u.role, u.is_active, u.created_at, t.id as tenant_id, t.name as tenant_name FROM users u LEFT JOIN tenants t ON t.owner_user_id = u.id WHERE u.id = $1', [decoded.user_id]);
    if (result.rows.length === 0) {
        msg.statusCode = 404;
        msg.payload = { error: 'Usuario no encontrado' };
        return msg;
    }
    msg.statusCode = 200;
    msg.payload = { success: true, user: result.rows[0] };
    return msg;
} catch (err) {
    node.error('Profile: ' + err.message, msg);
    msg.statusCode = 500;
    msg.payload = { error: 'Error interno del servidor' };
    return msg;
} finally {
    client.end().catch(()=>{});
}

