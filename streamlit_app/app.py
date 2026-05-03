"""IoT PdM - Labeling & Retrain UI.

Tabs:
  Overview      - live charts of recent features for the selected device
  Label Windows - tag time windows as healthy/light_imb/heavy_imb/loose
  Retrain       - kick off Isolation Forest retrain via the FastAPI /retrain endpoint
  Models        - inspect the model_versions registry
"""
from __future__ import annotations

import json
import os
from datetime import datetime, timedelta, timezone

import httpx
import pandas as pd
import plotly.express as px
import psycopg
import streamlit as st

DSN = (
    f"postgresql://{os.getenv('POSTGRES_USER', 'iotpdm')}:"
    f"{os.getenv('POSTGRES_PASSWORD', 'iotpdm-dev-change-me')}@"
    f"{os.getenv('POSTGRES_HOST', 'timescale')}:"
    f"{os.getenv('POSTGRES_PORT', '5432')}/"
    f"{os.getenv('POSTGRES_DB', 'iotpdm')}"
)
API = os.getenv("API_URL", "http://api:8080")
STATES = ["healthy", "light_imb", "heavy_imb", "loose"]

st.set_page_config(page_title="IoT PdM - Label & Retrain", layout="wide")
st.title("IoT Vibration PdM")
st.caption("Label recent windows | Retrain | Inspect model")


@st.cache_resource
def pg() -> psycopg.Connection:
    return psycopg.connect(DSN, autocommit=True)


def fetch(q: str, args: tuple = ()) -> pd.DataFrame:
    with pg().cursor() as cur:
        cur.execute(q, args)
        cols = [d.name for d in cur.description]
        rows = cur.fetchall()
    return pd.DataFrame(rows, columns=cols)


# ---------- Sidebar filters ----------
st.sidebar.header("Filters")
try:
    devices = fetch("SELECT device_id, asset FROM devices ORDER BY device_id")
except Exception as e:
    st.error(f"Database not reachable: {e}")
    st.stop()

if devices.empty:
    st.warning("No devices in the registry yet. Insert one into the `devices` table.")
    st.stop()

device_id = st.sidebar.selectbox("Device", devices["device_id"].tolist())
hours = st.sidebar.slider("Hours back", 1, 24, 2)
now = datetime.now(timezone.utc)
t0 = now - timedelta(hours=hours)

tab_overview, tab_label, tab_retrain, tab_model = st.tabs(
    ["Overview", "Label Windows", "Retrain", "Models"]
)

# ---------- Overview ----------
with tab_overview:
    df_feats = fetch(
        """
        SELECT time, rms_g, peak_hz, therm_max_c, anomaly_score, anomaly_flag, z_rms
        FROM features
        WHERE device_id = %s AND time > %s
        ORDER BY time ASC
        """,
        (device_id, t0),
    )
    if df_feats.empty:
        st.info("No data in this window yet. Connect the ESP32 and wait a few seconds.")
    else:
        col1, col2, col3, col4 = st.columns(4)
        col1.metric("Rows", f"{len(df_feats):,}")
        col2.metric("Anomalies", int(df_feats["anomaly_flag"].fillna(False).sum()))
        col3.metric("Peak RMS (g)", round(df_feats["rms_g"].max(), 3))
        col4.metric("Max Therm C", round(df_feats["therm_max_c"].max(), 1))

        fig = px.line(df_feats, x="time", y="rms_g", title="RMS acceleration (g)")
        anom = df_feats[df_feats["anomaly_flag"] == True]  # noqa: E712
        if not anom.empty:
            fig.add_scatter(
                x=anom["time"],
                y=anom["rms_g"],
                mode="markers",
                name="anomaly",
                marker=dict(color="red", size=8),
            )
        st.plotly_chart(fig, use_container_width=True)

        st.plotly_chart(
            px.scatter(
                df_feats,
                x="peak_hz",
                y="rms_g",
                color="anomaly_score",
                title="Peak frequency vs RMS (color = anomaly score)",
            ),
            use_container_width=True,
        )

# ---------- Label ----------
with tab_label:
    st.subheader("Label a window")
    c1, c2 = st.columns(2)
    with c1:
        label_start = st.text_input("Window start (UTC ISO)", value=t0.isoformat())
    with c2:
        label_end = st.text_input("Window end (UTC ISO)", value=now.isoformat())

    state = st.selectbox("Asset state", STATES, index=0)
    notes = st.text_input("Notes (optional)")
    if st.button("Save label"):
        try:
            ws = datetime.fromisoformat(label_start.replace("Z", "+00:00"))
            we = datetime.fromisoformat(label_end.replace("Z", "+00:00"))
            with pg().cursor() as cur:
                cur.execute(
                    "INSERT INTO labels "
                    "(device_id, window_start, window_end, state, notes, labeled_by) "
                    "VALUES (%s, %s, %s, %s, %s, 'streamlit')",
                    (device_id, ws, we, state, notes),
                )
            st.success(f"Labeled [{ws} -> {we}] as {state}")
        except Exception as e:
            st.error(f"Failed: {e}")

    st.markdown("#### Existing labels")
    st.dataframe(
        fetch(
            "SELECT labeled_at, device_id, window_start, window_end, state, notes "
            "FROM labels WHERE device_id = %s ORDER BY labeled_at DESC LIMIT 50",
            (device_id,),
        ),
        use_container_width=True,
    )

# ---------- Retrain ----------
with tab_retrain:
    st.subheader("Retrain the anomaly model")
    c1, c2, c3 = st.columns(3)
    hours_for_train = c1.number_input("Training window (hours)", 1, 168, 3)
    contamination = c2.selectbox("Contamination", ["auto", "0.01", "0.05", "0.10"], index=0)
    activate = c3.checkbox("Activate after training", value=True)

    if st.button("Retrain now"):
        with st.spinner("Training..."):
            try:
                r = httpx.post(
                    f"{API}/retrain",
                    json={
                        "hours": int(hours_for_train),
                        "contamination": contamination,
                        "activate": activate,
                    },
                    timeout=180,
                )
                if r.status_code == 200:
                    st.success("Training complete - model is live.")
                    st.code(r.json().get("stdout", ""), language="text")
                else:
                    st.error(f"Failed: {r.status_code}")
                    st.code(r.text)
            except Exception as e:
                st.error(str(e))

    st.markdown("#### Recent feature distribution")
    df_sanity = fetch(
        "SELECT rms_g, peak_hz, therm_max_c, anomaly_score FROM features "
        "WHERE device_id = %s AND time > now() - interval '1 hour' LIMIT 5000",
        (device_id,),
    )
    if not df_sanity.empty:
        st.plotly_chart(
            px.histogram(
                df_sanity, x="anomaly_score", nbins=40, title="Anomaly score distribution"
            ),
            use_container_width=True,
        )

# ---------- Model registry ----------
with tab_model:
    st.subheader("Model versions")
    df_models = fetch(
        "SELECT version, algorithm, trained_at, trained_on_rows, active, metrics_json "
        "FROM model_versions ORDER BY trained_at DESC LIMIT 20"
    )
    if not df_models.empty:
        st.dataframe(df_models.drop(columns=["metrics_json"]), use_container_width=True)
        st.markdown("#### Latest model metrics")
        latest_metrics = df_models.iloc[0]["metrics_json"]
        if isinstance(latest_metrics, str):
            latest_metrics = json.loads(latest_metrics)
        st.json(latest_metrics)
    else:
        st.info("No models trained yet - run a retrain on the previous tab.")
