use anyhow::Result;
use tracing_subscriber;

#[tokio::main]
async fn main() -> Result<()> {
    tracing_subscriber::fmt::init();

    let nc = async_nats::connect("nats://localhost:4222").await?;

    let tick_rx = nc.subscribe("orchestrator.tick").await?;

    let mut controller = gnc::controller::GNCController::new(nc);

    tracing::info!("GNC controller starting");

    tokio::select! {
        res = controller.run(tick_rx) => {
            if let Err(e) = res {
                tracing::error!(error = %e, "GNC controller exited with error");
                return Err(e);
            }
        }
        _ = tokio::signal::ctrl_c() => {
            tracing::info!("Received shutdown signal, stopping GNC");
        }
    }

    tracing::info!("GNC controller stopped");
    Ok(())
}
