import { panel, waitFor, sleep } from '../lib.mjs';

/** Robotics: forward kinematics and joint-space trajectory planning. */
export const name = 'robotics';

export async function run({ cdp, check }) {
  const p = panel(cdp, /ROBOTICS/i);
  await p.scrollIntoView();

  // --- forward kinematics
  await p.fill('JOINT_ANGLES (RAD)', '0.5, -0.3');
  await p.click('FORWARD KINEMATICS');
  const fk = await waitFor(p.text, 'JOINT_COUNT');
  check('kinematics returns a joint count', Boolean(fk) && /JOINT_COUNT/.test(fk));
  check(
    'end-effector position rendered',
    Boolean(fk) && /EE_X_M/.test(fk) && /EE_Y_M/.test(fk),
  );
  check('DH frames listed', Boolean(fk) && /link\d/i.test(fk));

  // --- trajectory planning switches the rendered shape
  await p.fill('JOINT_START', '0.0, 0.0');
  await p.fill('JOINT_GOAL', '1.0, -1.0');
  await p.fill('DURATION_S', '1.0');
  await p.click('PLAN TRAJECTORY');
  const traj = await waitFor(p.text, 'GOAL_REACHED');
  check('trajectory reports goal_reached', Boolean(traj) && /GOAL_REACHED/.test(traj));
  check('trajectory reports sample count', Boolean(traj) && /SAMPLE_COUNT/.test(traj));

  // --- bad joint input must be refused, not sent as NaN
  await p.fill('JOINT_ANGLES (RAD)', 'not numbers');
  await sleep(600);
  const bad = await p.text();
  check(
    'bad joint input is caught before the request',
    /comma or space separated numbers/i.test(bad),
    'no validation notice',
  );
}