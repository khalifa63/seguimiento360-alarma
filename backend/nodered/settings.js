/**
 * Node-RED Settings - Seguimiento 360
 * Generado: 2026-08-08
 * Fuente de verdad: backend/nodered/settings.js
 */

module.exports = {
    flowFile: 'flows.json',
    credentialSecret: process.env.NODE_RED_CREDENTIAL_SECRET,
    flowFilePretty: true,

    functionGlobalContext: {
        pg: require('pg'),
        bcrypt: require('bcryptjs'),
        jwt: require('jsonwebtoken'),
        dbConfig: {
            host: 'postgres',
            port: 5432,
            database: 'seguimiento360_alarmas',
            user: 'admin',
            password: process.env.PG_PASSWORD
        },
        jwtSecret: process.env.JWT_SECRET
    },

    exportGlobalContextKeys: false,

    externalModules: {
        modules: {
            allowInstall: true,
            allowList: '*',
            denyList: []
        }
    },

    uiPort: process.env.PORT || 1880,
    mqttReconnectTime: 15000,
    serialReconnectTime: 15000,
    debugMaxLength: 1000,

    httpNodeRoot: "/",

    httpNodeCors: {
        origin: "*",
        methods: "GET,HEAD,PUT,PATCH,POST,DELETE,OPTIONS",
        preflightContinue: false,
        credentials: true
    },

    httpNodeMiddleware: function(req, res, next) {
        res.setHeader('Access-Control-Allow-Origin', '*');
        res.setHeader('Access-Control-Allow-Methods', 'GET,PUT,POST,DELETE,OPTIONS');
        res.setHeader('Access-Control-Allow-Headers', 'Content-Type,Authorization');
        if (req.method === 'OPTIONS') {
            res.statusCode = 204;
            res.end();
            return;
        }
        next();
    },

    editorTheme: {
        projects: { enabled: false },
        tours: false
    },

    logging: {
        console: { level: "info", audit: false }
    }
};
