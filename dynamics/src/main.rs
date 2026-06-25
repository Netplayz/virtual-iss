use dynamics::engine::DynamicsEngine;
use dynamics::vehicle::VehicleConfig;
use anyhow::Result;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt::init();

    let nats_url =
        std::env::var("NATS_URL").unwrap_or_else(|_| "nats://localhost:4222".to_string());
    tracing::info!("Connecting to NATS at {}", nats_url);

    let nats_client = async_nats::connect(&nats_url).await?;
    let vehicle_config = VehicleConfig::default();
    let mut engine = DynamicsEngine::new(nats_client, vehicle_config);

    tokio::select! {
        result = engine.run() => {
            if let Err(e) = result {
                tracing::error!("Dynamics engine exited with error: {}", e);
            }
        }
        _ = tokio::signal::ctrl_c() => {
            tracing::info!("Received Ctrl+C, shutting down...");
        }
    }

    Ok(())
}
