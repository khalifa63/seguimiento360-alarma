--
-- PostgreSQL database dump
--

\restrict Beupvd1tPFVWgQMMqQyO1WUNQsZVsbUNWTpTccvJsX5zjEd8nWCAdqvX8onBRoP

-- Dumped from database version 16.14
-- Dumped by pg_dump version 16.14

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: alarm_state; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.alarm_state (
    site_id integer NOT NULL,
    hub_id integer,
    state character varying(16) DEFAULT 'disarmed'::character varying NOT NULL,
    updated_at timestamp with time zone DEFAULT now() NOT NULL,
    updated_by integer
);


--
-- Name: TABLE alarm_state; Type: COMMENT; Schema: public; Owner: -
--

COMMENT ON TABLE public.alarm_state IS 'Estado actual de alarma por sitio.';


--
-- Name: claim_codes; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.claim_codes (
    id integer NOT NULL,
    code character varying(12) NOT NULL,
    device_type character varying(16) NOT NULL,
    status character varying(16) DEFAULT 'active'::character varying NOT NULL,
    created_by integer,
    tenant_id integer,
    site_id integer,
    claimed_device_id integer,
    claimed_at timestamp with time zone,
    expires_at timestamp with time zone,
    created_at timestamp with time zone DEFAULT now() NOT NULL
);


--
-- Name: claim_codes_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.claim_codes_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: claim_codes_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.claim_codes_id_seq OWNED BY public.claim_codes.id;


--
-- Name: device_audit_log; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.device_audit_log (
    id integer NOT NULL,
    device_id integer,
    tenant_id integer,
    action character varying(50) NOT NULL,
    performed_by integer,
    details jsonb,
    created_at timestamp without time zone DEFAULT now() NOT NULL
);


--
-- Name: COLUMN device_audit_log.device_id; Type: COMMENT; Schema: public; Owner: -
--

COMMENT ON COLUMN public.device_audit_log.device_id IS 'Puede ser NULL para acciones que no refieren a device específico (ej: alarm_arm sobre hub)';


--
-- Name: device_audit_log_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.device_audit_log_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: device_audit_log_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.device_audit_log_id_seq OWNED BY public.device_audit_log.id;


--
-- Name: device_credentials; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.device_credentials (
    id integer NOT NULL,
    device_id integer NOT NULL,
    mqtt_user character varying(150) NOT NULL,
    mqtt_password_hash character varying(255) NOT NULL,
    created_at timestamp without time zone DEFAULT now() NOT NULL,
    rotated_at timestamp without time zone
);


--
-- Name: device_credentials_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.device_credentials_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: device_credentials_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.device_credentials_id_seq OWNED BY public.device_credentials.id;


--
-- Name: devices; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.devices (
    id integer NOT NULL,
    serial_number character varying(100) NOT NULL,
    device_type character varying(50) NOT NULL,
    firmware_version character varying(50),
    tenant_id integer,
    site_id integer,
    status character varying(50) DEFAULT 'available'::character varying NOT NULL,
    bound_at timestamp without time zone,
    unbound_at timestamp without time zone,
    created_at timestamp without time zone DEFAULT now() NOT NULL
);


--
-- Name: devices_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.devices_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: devices_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.devices_id_seq OWNED BY public.devices.id;


--
-- Name: events; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.events (
    id bigint NOT NULL,
    tenant_id integer NOT NULL,
    site_id integer,
    hub_id integer,
    hub_mac character varying(32),
    node_mac character varying(17),
    node_name character varying(255),
    event_type character varying(50) NOT NULL,
    battery_level integer,
    value integer,
    sequence_number integer,
    payload jsonb,
    created_at timestamp with time zone DEFAULT now() NOT NULL
);


--
-- Name: events_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.events_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: events_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.events_id_seq OWNED BY public.events.id;


--
-- Name: hubs; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.hubs (
    id integer NOT NULL,
    tenant_id integer NOT NULL,
    site_id integer NOT NULL,
    serial_number character varying(32) NOT NULL,
    device_type character varying(16) DEFAULT 'alarm_hub'::character varying NOT NULL,
    name character varying(64),
    status character varying(16) DEFAULT 'pending'::character varying NOT NULL,
    last_seen timestamp with time zone,
    created_at timestamp with time zone DEFAULT now() NOT NULL
);


--
-- Name: TABLE hubs; Type: COMMENT; Schema: public; Owner: -
--

COMMENT ON TABLE public.hubs IS 'Dispositivos ESP32. Status pending=registrado sin confirmar, active=online.';


--
-- Name: hubs_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.hubs_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: hubs_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.hubs_id_seq OWNED BY public.hubs.id;


--
-- Name: nodes; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.nodes (
    id integer NOT NULL,
    hub_id integer NOT NULL,
    mac_address character varying(17) NOT NULL,
    name character varying(64) DEFAULT 'Nodo Sin Nombre'::character varying,
    location character varying(64) DEFAULT 'Sin Ubicación'::character varying,
    room character varying(64) DEFAULT 'Sin Habitación'::character varying,
    distance_threshold integer DEFAULT 50,
    status character varying(20) DEFAULT 'pending'::character varying,
    last_seen timestamp with time zone,
    created_at timestamp with time zone DEFAULT now(),
    updated_at timestamp with time zone DEFAULT now(),
    buzzer_seconds smallint,
    CONSTRAINT nodes_distance_threshold_check CHECK (((distance_threshold >= 2) AND (distance_threshold <= 200))),
    CONSTRAINT nodes_status_check CHECK (((status)::text = ANY ((ARRAY['pending'::character varying, 'active'::character varying, 'inactive'::character varying])::text[])))
);


--
-- Name: COLUMN nodes.buzzer_seconds; Type: COMMENT; Schema: public; Owner: -
--

COMMENT ON COLUMN public.nodes.buzzer_seconds IS 'Duracion de sirena por evento (s); 0 = desactivado, NULL = default 30 (HUB-IF-015)';


--
-- Name: nodes_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.nodes_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: nodes_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.nodes_id_seq OWNED BY public.nodes.id;


--
-- Name: sites; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.sites (
    id integer NOT NULL,
    tenant_id integer NOT NULL,
    name character varying(255) NOT NULL,
    address text,
    latitude numeric(10,8),
    longitude numeric(11,8),
    is_active boolean DEFAULT true NOT NULL,
    created_at timestamp without time zone DEFAULT now() NOT NULL
);


--
-- Name: sites_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.sites_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: sites_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.sites_id_seq OWNED BY public.sites.id;


--
-- Name: tenants; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.tenants (
    id integer NOT NULL,
    name character varying(255) NOT NULL,
    owner_user_id integer NOT NULL,
    is_active boolean DEFAULT true NOT NULL,
    created_at timestamp without time zone DEFAULT now() NOT NULL
);


--
-- Name: tenants_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.tenants_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: tenants_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.tenants_id_seq OWNED BY public.tenants.id;


--
-- Name: user_fcm_tokens; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.user_fcm_tokens (
    id integer NOT NULL,
    user_id integer,
    token jsonb,
    updated_at timestamp without time zone DEFAULT now(),
    tenant_id integer
);


--
-- Name: user_fcm_tokens_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.user_fcm_tokens_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: user_fcm_tokens_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.user_fcm_tokens_id_seq OWNED BY public.user_fcm_tokens.id;


--
-- Name: users; Type: TABLE; Schema: public; Owner: -
--

CREATE TABLE public.users (
    id integer NOT NULL,
    email character varying(255) NOT NULL,
    password_hash character varying(255) NOT NULL,
    full_name character varying(255),
    phone character varying(50),
    role character varying(50) DEFAULT 'customer'::character varying NOT NULL,
    is_active boolean DEFAULT true NOT NULL,
    created_at timestamp without time zone DEFAULT now() NOT NULL,
    updated_at timestamp without time zone DEFAULT now() NOT NULL,
    password character varying(255),
    nombre character varying(100),
    tenant_id integer DEFAULT 1
);


--
-- Name: users_id_seq; Type: SEQUENCE; Schema: public; Owner: -
--

CREATE SEQUENCE public.users_id_seq
    AS integer
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


--
-- Name: users_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: -
--

ALTER SEQUENCE public.users_id_seq OWNED BY public.users.id;


--
-- Name: claim_codes id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes ALTER COLUMN id SET DEFAULT nextval('public.claim_codes_id_seq'::regclass);


--
-- Name: device_audit_log id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_audit_log ALTER COLUMN id SET DEFAULT nextval('public.device_audit_log_id_seq'::regclass);


--
-- Name: device_credentials id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_credentials ALTER COLUMN id SET DEFAULT nextval('public.device_credentials_id_seq'::regclass);


--
-- Name: devices id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.devices ALTER COLUMN id SET DEFAULT nextval('public.devices_id_seq'::regclass);


--
-- Name: events id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.events ALTER COLUMN id SET DEFAULT nextval('public.events_id_seq'::regclass);


--
-- Name: hubs id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.hubs ALTER COLUMN id SET DEFAULT nextval('public.hubs_id_seq'::regclass);


--
-- Name: nodes id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.nodes ALTER COLUMN id SET DEFAULT nextval('public.nodes_id_seq'::regclass);


--
-- Name: sites id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sites ALTER COLUMN id SET DEFAULT nextval('public.sites_id_seq'::regclass);


--
-- Name: tenants id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.tenants ALTER COLUMN id SET DEFAULT nextval('public.tenants_id_seq'::regclass);


--
-- Name: user_fcm_tokens id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_fcm_tokens ALTER COLUMN id SET DEFAULT nextval('public.user_fcm_tokens_id_seq'::regclass);


--
-- Name: users id; Type: DEFAULT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.users ALTER COLUMN id SET DEFAULT nextval('public.users_id_seq'::regclass);


--
-- Name: alarm_state alarm_state_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.alarm_state
    ADD CONSTRAINT alarm_state_pkey PRIMARY KEY (site_id);


--
-- Name: claim_codes claim_codes_code_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes
    ADD CONSTRAINT claim_codes_code_key UNIQUE (code);


--
-- Name: claim_codes claim_codes_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes
    ADD CONSTRAINT claim_codes_pkey PRIMARY KEY (id);


--
-- Name: device_audit_log device_audit_log_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_audit_log
    ADD CONSTRAINT device_audit_log_pkey PRIMARY KEY (id);


--
-- Name: device_credentials device_credentials_mqtt_user_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_credentials
    ADD CONSTRAINT device_credentials_mqtt_user_key UNIQUE (mqtt_user);


--
-- Name: device_credentials device_credentials_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_credentials
    ADD CONSTRAINT device_credentials_pkey PRIMARY KEY (id);


--
-- Name: devices devices_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.devices
    ADD CONSTRAINT devices_pkey PRIMARY KEY (id);


--
-- Name: devices devices_serial_number_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.devices
    ADD CONSTRAINT devices_serial_number_key UNIQUE (serial_number);


--
-- Name: events events_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.events
    ADD CONSTRAINT events_pkey PRIMARY KEY (id);


--
-- Name: hubs hubs_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.hubs
    ADD CONSTRAINT hubs_pkey PRIMARY KEY (id);


--
-- Name: hubs hubs_serial_number_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.hubs
    ADD CONSTRAINT hubs_serial_number_key UNIQUE (serial_number);


--
-- Name: nodes nodes_mac_address_unique; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.nodes
    ADD CONSTRAINT nodes_mac_address_unique UNIQUE (mac_address);


--
-- Name: nodes nodes_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.nodes
    ADD CONSTRAINT nodes_pkey PRIMARY KEY (id);


--
-- Name: sites sites_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sites
    ADD CONSTRAINT sites_pkey PRIMARY KEY (id);


--
-- Name: tenants tenants_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.tenants
    ADD CONSTRAINT tenants_pkey PRIMARY KEY (id);


--
-- Name: user_fcm_tokens user_fcm_tokens_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.user_fcm_tokens
    ADD CONSTRAINT user_fcm_tokens_pkey PRIMARY KEY (id);


--
-- Name: users users_email_key; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.users
    ADD CONSTRAINT users_email_key UNIQUE (email);


--
-- Name: users users_pkey; Type: CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.users
    ADD CONSTRAINT users_pkey PRIMARY KEY (id);


--
-- Name: devices_serial_lower_idx; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX devices_serial_lower_idx ON public.devices USING btree (lower((serial_number)::text));


--
-- Name: devices_serial_number_uidx; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX devices_serial_number_uidx ON public.devices USING btree (serial_number);


--
-- Name: idx_audit_created; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_audit_created ON public.device_audit_log USING btree (created_at);


--
-- Name: idx_audit_device; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_audit_device ON public.device_audit_log USING btree (device_id);


--
-- Name: idx_devices_serial; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_devices_serial ON public.devices USING btree (serial_number);


--
-- Name: idx_devices_status; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_devices_status ON public.devices USING btree (status);


--
-- Name: idx_devices_tenant; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_devices_tenant ON public.devices USING btree (tenant_id);


--
-- Name: idx_events_node; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_events_node ON public.events USING btree (node_mac, created_at DESC);


--
-- Name: idx_events_tenant_time; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_events_tenant_time ON public.events USING btree (tenant_id, created_at DESC);


--
-- Name: idx_events_type; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_events_type ON public.events USING btree (tenant_id, event_type, created_at DESC);


--
-- Name: idx_hubs_serial; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_hubs_serial ON public.hubs USING btree (serial_number);


--
-- Name: idx_hubs_site; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_hubs_site ON public.hubs USING btree (site_id);


--
-- Name: idx_hubs_tenant; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_hubs_tenant ON public.hubs USING btree (tenant_id);


--
-- Name: idx_nodes_hub_id; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_nodes_hub_id ON public.nodes USING btree (hub_id);


--
-- Name: idx_nodes_mac; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_nodes_mac ON public.nodes USING btree (mac_address);


--
-- Name: idx_sites_tenant; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_sites_tenant ON public.sites USING btree (tenant_id);


--
-- Name: idx_tenants_owner; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_tenants_owner ON public.tenants USING btree (owner_user_id);


--
-- Name: idx_users_email; Type: INDEX; Schema: public; Owner: -
--

CREATE INDEX idx_users_email ON public.users USING btree (email);


--
-- Name: user_fcm_tokens_endpoint_uidx; Type: INDEX; Schema: public; Owner: -
--

CREATE UNIQUE INDEX user_fcm_tokens_endpoint_uidx ON public.user_fcm_tokens USING btree (((token ->> 'endpoint'::text)));


--
-- Name: alarm_state alarm_state_hub_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.alarm_state
    ADD CONSTRAINT alarm_state_hub_id_fkey FOREIGN KEY (hub_id) REFERENCES public.hubs(id);


--
-- Name: alarm_state alarm_state_site_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.alarm_state
    ADD CONSTRAINT alarm_state_site_id_fkey FOREIGN KEY (site_id) REFERENCES public.sites(id);


--
-- Name: alarm_state alarm_state_updated_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.alarm_state
    ADD CONSTRAINT alarm_state_updated_by_fkey FOREIGN KEY (updated_by) REFERENCES public.users(id);


--
-- Name: claim_codes claim_codes_claimed_device_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes
    ADD CONSTRAINT claim_codes_claimed_device_id_fkey FOREIGN KEY (claimed_device_id) REFERENCES public.devices(id);


--
-- Name: claim_codes claim_codes_created_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes
    ADD CONSTRAINT claim_codes_created_by_fkey FOREIGN KEY (created_by) REFERENCES public.users(id);


--
-- Name: claim_codes claim_codes_site_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes
    ADD CONSTRAINT claim_codes_site_id_fkey FOREIGN KEY (site_id) REFERENCES public.sites(id);


--
-- Name: claim_codes claim_codes_tenant_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.claim_codes
    ADD CONSTRAINT claim_codes_tenant_id_fkey FOREIGN KEY (tenant_id) REFERENCES public.tenants(id);


--
-- Name: device_audit_log device_audit_log_device_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_audit_log
    ADD CONSTRAINT device_audit_log_device_id_fkey FOREIGN KEY (device_id) REFERENCES public.devices(id) ON DELETE SET NULL;


--
-- Name: device_audit_log device_audit_log_performed_by_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_audit_log
    ADD CONSTRAINT device_audit_log_performed_by_fkey FOREIGN KEY (performed_by) REFERENCES public.users(id);


--
-- Name: device_credentials device_credentials_device_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.device_credentials
    ADD CONSTRAINT device_credentials_device_id_fkey FOREIGN KEY (device_id) REFERENCES public.devices(id) ON DELETE CASCADE;


--
-- Name: devices devices_site_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.devices
    ADD CONSTRAINT devices_site_id_fkey FOREIGN KEY (site_id) REFERENCES public.sites(id) ON DELETE SET NULL;


--
-- Name: devices devices_tenant_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.devices
    ADD CONSTRAINT devices_tenant_id_fkey FOREIGN KEY (tenant_id) REFERENCES public.tenants(id) ON DELETE SET NULL;


--
-- Name: events events_tenant_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.events
    ADD CONSTRAINT events_tenant_id_fkey FOREIGN KEY (tenant_id) REFERENCES public.tenants(id) ON DELETE CASCADE;


--
-- Name: hubs hubs_site_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.hubs
    ADD CONSTRAINT hubs_site_id_fkey FOREIGN KEY (site_id) REFERENCES public.sites(id);


--
-- Name: hubs hubs_tenant_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.hubs
    ADD CONSTRAINT hubs_tenant_id_fkey FOREIGN KEY (tenant_id) REFERENCES public.tenants(id);


--
-- Name: nodes nodes_hub_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.nodes
    ADD CONSTRAINT nodes_hub_id_fkey FOREIGN KEY (hub_id) REFERENCES public.hubs(id) ON DELETE CASCADE;


--
-- Name: sites sites_tenant_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.sites
    ADD CONSTRAINT sites_tenant_id_fkey FOREIGN KEY (tenant_id) REFERENCES public.tenants(id) ON DELETE CASCADE;


--
-- Name: tenants tenants_owner_user_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: -
--

ALTER TABLE ONLY public.tenants
    ADD CONSTRAINT tenants_owner_user_id_fkey FOREIGN KEY (owner_user_id) REFERENCES public.users(id) ON DELETE RESTRICT;


--
-- PostgreSQL database dump complete
--

\unrestrict Beupvd1tPFVWgQMMqQyO1WUNQsZVsbUNWTpTccvJsX5zjEd8nWCAdqvX8onBRoP

